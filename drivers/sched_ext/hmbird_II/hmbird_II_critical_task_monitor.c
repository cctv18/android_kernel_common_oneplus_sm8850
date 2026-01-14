// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Oplus. All rights reserved.
 */
#include <trace/events/sched.h>
#include <trace/hooks/sched.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/sort.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/spinlock.h>
#include <linux/time.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/list_sort.h>
#include "hmbird_II_sysctl.h"
#include "hmbird_II_critical_task_monitor.h"

#define MAX_CRITICAL_NUM 	10
#define MAX_NODE_COUNT		64
#define TASK_COMM_MIN_LEN 	3
#define MIN_WAKE_CNT		10
#define UTIL_INTERVAL_JIFF	msecs_to_jiffies(8000)
#define CTM_INTERNAL_DEBUG	0

static atomic_t g_ctm_enable = ATOMIC_INIT(0);
static atomic_t target_tgid = ATOMIC_INIT(-1);
static int need_search_sf = 0;
static struct timer_list ctm_timer;
static int task_node_cnt = 0;

/* tasks for wake scanning, filter by critical_task name */
struct cand_crit_node {
	pid_t pid;
	u32 waker_count;
	u32 wakee_count;
	int configed;
	struct list_head list_node;
};

/* user set critical tasks */
struct uset_crit_task {
	char name[TASK_COMM_LEN + 1];
	struct sched_prop_map sched_map;
	struct list_head cand_list;
} u_crit_tasks[MAX_CRITICAL_NUM];
static int u_crit_cnt = 0;

/* user set critical pids, directly set sched_prop */
struct uset_crit_pid {
	int u_pid;
	struct sched_prop_map sched_map;
	int configed;
	char p_comm[TASK_COMM_LEN + 1];
} u_crit_pids[MAX_CRITICAL_NUM];
static int u_pid_cnt = 0;

struct uset_crit_pid sf_search_prop;

static DEFINE_RWLOCK(uset_crit_task_rwlock);
static DEFINE_RWLOCK(cand_task_rwlock);

static void ctm_reset_ilocked(void);
static void remove_all_list_node_ilocked(void);
static void add_to_cand_ilocked(struct list_head *cand_list_p, pid_t pid);
extern int internal_cfg_task_sched_prop_handler(pid_t pid, struct sched_prop_map sched_map);


/* enable ctm - critical_task_monitor */
static int ctm_switch(int enable) {
	write_lock(&cand_task_rwlock);
	if (enable) {
		atomic_set(&g_ctm_enable, 1);
		mod_timer(&ctm_timer, jiffies + UTIL_INTERVAL_JIFF);
		CTM_INFO("ctm enabled\n");
	} else {
		atomic_set(&g_ctm_enable, 0);
		/* if we need unset_all_hmbird_II_policy, maybe set default sched_prop in the future */
		remove_all_list_node_ilocked();
		ctm_reset_ilocked();
		del_timer(&ctm_timer);
		CTM_INFO("ctm disabled\n");
	}
	write_unlock(&cand_task_rwlock);
	return 0;
}

static int set_target_tgid(int cfg_type, pid_t pid)
{
	struct task_struct *task;
	if (cfg_type == CTM_DISABLE_FLAG) {
		ctm_switch(0);
		return 0;
	}
	rcu_read_lock();
	task = get_pid_task(find_vpid(pid), PIDTYPE_PID);
	if (!task) {
		rcu_read_unlock();
		CTM_INFO("task not found\n");
		return -1;
	}
	rcu_read_unlock();
	if (task->tgid != task->pid) {
		CTM_INFO("task is not main thread\n");
		put_task_struct(task);
		return -1;
	}

	atomic_set(&target_tgid, task->tgid);
	CTM_INFO("target_tgid= %d has set", task->tgid);
	put_task_struct(task);
	if (atomic_read(&target_tgid) > 0 && atomic_read(&g_ctm_enable) == 0) {
		ctm_switch(1);
	}
	return 0;
}

static int get_task_name_len(const char *name) {
	int name_len = strlen(name);

	if (name_len > TASK_COMM_LEN) {
		return TASK_COMM_LEN;
	} else if (name_len < TASK_COMM_MIN_LEN) {
		return -1;
	}
	return name_len;
}

static inline int add_critical_name(pid_t pid, const char *name, struct sched_prop_map sched_map)
{
	int i, name_len, ret = 0;

	if (atomic_read(&target_tgid) <= 0 || atomic_read(&g_ctm_enable) == 0) {
		ret = -1;
		return ret;
	}
	if (u_crit_cnt >= MAX_CRITICAL_NUM - 1) {
		ret = -1;
		return ret;
	}
	if (strlen(name) < TASK_COMM_MIN_LEN) {
		ret = -1;
		return ret;
	}
	name_len = get_task_name_len(name);
	if (name_len <= 0) {
		ret = -1;
		return ret;
	}
	write_lock(&uset_crit_task_rwlock);
	for (i = 0; i < u_crit_cnt; i++) {
		if(0 == strncmp(u_crit_tasks[i].name, name, name_len)) {
			ret = -1;
			goto unlock;
		}
	}
	strncpy((char *)u_crit_tasks[u_crit_cnt].name, name, name_len);
	u_crit_tasks[u_crit_cnt].name[strlen(name)] = '\0';
	u_crit_tasks[u_crit_cnt].sched_map = sched_map;
	u_crit_cnt++;

	CTM_INFO("critical name(%s) has set  %llx, %llx\n", name,
		sched_map.sched_prop, sched_map.sched_prop_mask);
unlock:
	write_unlock(&uset_crit_task_rwlock);
	return ret;
}

static inline int add_critical_pid(pid_t pid, struct sched_prop_map sched_map)
{
	int i, name_len, ret = 0;
	struct task_struct *task;

	if (atomic_read(&g_ctm_enable) == 0) {
		ret = -1;
		return ret;
	}
	if (u_pid_cnt >= MAX_CRITICAL_NUM - 1) {
		ret = -1;
		return ret;
	}
	for (i = 0; i < u_pid_cnt; i++) {
		if (pid == u_crit_pids[i].u_pid) {
			ret = -1;
			return ret;
		}
	}
	rcu_read_lock();
	task = get_pid_task(find_vpid(pid), PIDTYPE_PID);
	if (!task) {
		rcu_read_unlock();
		CTM_INFO("task not found\n");
		ret = -1;
		return ret;
	}
	rcu_read_unlock();
	u_crit_pids[u_pid_cnt].u_pid = pid;
	u_crit_pids[u_pid_cnt].sched_map = sched_map;

	name_len = get_task_name_len(task->comm);
	if (name_len < 0) {
		strncpy((char *)u_crit_pids[u_pid_cnt].p_comm, "abnormal_comm", strlen("abnormal_comm"));
	} else {
		strncpy((char *)u_crit_pids[u_pid_cnt].p_comm, task->comm, name_len);
	}
	u_crit_pids[u_pid_cnt].p_comm[strlen(task->comm)] = '\0';
	u_crit_pids[u_pid_cnt].configed = NOT_CONFIG;
	internal_cfg_task_sched_prop_handler(pid, sched_map);
	u_crit_pids[u_pid_cnt].configed = CONFIGED;
	u_pid_cnt++;
	put_task_struct(task);
	CTM_INFO("critical pid(%d) has set  %llx, %llx\n", pid,
		sched_map.sched_prop, sched_map.sched_prop_mask);
	return ret;
}

int set_critical_task(struct critical_task_params *params)
{
	if (CTM_DISABLE_FLAG == params->type && (1 == params->param_nums || 2 == params->param_nums)) {
		ctm_switch(0);
	} else if (APP_MAIN_TGID == params->type && 2 == params->param_nums) {
		set_target_tgid(params->type, params->pid);
	} else if (5 == params->param_nums) {
		if(APP_MAIN_TGID == params->type) {
			if (params->pid <= 0)
				add_critical_name(params->pid, params->name, params->sched_map);
			else
				add_critical_pid(params->pid, params->sched_map);
		} else if (SF_TYPE == params->type) {
			if (params->pid <= 0) {
				need_search_sf = 1;
				sf_search_prop.u_pid = -1;
				sf_search_prop.sched_map = params->sched_map;
			} else {
				need_search_sf = 0;
				add_critical_pid(params->pid, params->sched_map);
				CTM_INFO("set surfaceflinger %d to critical_pid\n", params->pid);
			}
		}
	} else {
		pr_warn("critical_task got unexpcept input\n");
		return -1;
	}
	return 0;
}

/* output critical name + pid + cnt */
int get_critical_task_list(char *result_buf, int buf_size)
{
	int i, len = 0;
	struct list_head *cand_list;
	struct cand_crit_node *entry;
	if (0 == atomic_read(&g_ctm_enable)) {
		len += snprintf(result_buf + len, buf_size - len, "critical task monitor off\n");
		return 0;
	}
	read_lock(&cand_task_rwlock);
	len += snprintf(result_buf + len, buf_size - len, "CriticalTasks\n");
	for (i = 0; i < u_crit_cnt; i++) {
		len += snprintf(result_buf + len, buf_size - len, " %s:\n", u_crit_tasks[i].name);
		cand_list = &u_crit_tasks[i].cand_list;
		list_for_each_entry(entry, cand_list, list_node) {
			len += snprintf(result_buf + len, buf_size - len, " %d\t %u\t %u\t %llx\t %llx\t %d\n",
				entry->pid, entry->wakee_count, entry->waker_count, u_crit_tasks[i].sched_map.sched_prop,
				u_crit_tasks[i].sched_map.sched_prop_mask, entry->configed);
		}
	}
	if (u_pid_cnt > 0)
		len += snprintf(result_buf + len, buf_size - len, "CriticalPids\n");
	for (i = 0; i < u_pid_cnt; i++) {
		len += snprintf(result_buf + len, buf_size - len,
			" %d\t %s %llx\t %llx\t %d\n",
			u_crit_pids[i].u_pid, u_crit_pids[i].p_comm, u_crit_pids[i].sched_map.sched_prop,
			u_crit_pids[i].sched_map.sched_prop_mask, u_crit_pids[i].configed);
	}

	read_unlock(&cand_task_rwlock);
	if (need_search_sf) {
		len += snprintf(result_buf + len, buf_size - len, "Find surfaceflinger: %d\t %llx\t %llx\t %d\n",
			sf_search_prop.u_pid, sf_search_prop.sched_map.sched_prop,
			sf_search_prop.sched_map.sched_prop_mask, sf_search_prop.configed);
	}
	return 0;
}

static void init_cand_node(struct cand_crit_node *new_node, pid_t pid)
{
	if (new_node) {
		new_node->pid = pid;
		new_node->waker_count = 0;
		new_node->wakee_count = 0;
		new_node->configed = NOT_CONFIG;
	}
}

static void ctm_reset_ilocked(void)
{
	int i;
	for (i = 0; i < MAX_CRITICAL_NUM; i++) {
		u_crit_tasks[i].sched_map.sched_prop = 0;
		u_crit_tasks[i].sched_map.sched_prop_mask = 0;
		u_crit_tasks[i].name[0] = '\0';
	}
	u_crit_cnt = 0;
	u_pid_cnt = 0;
	task_node_cnt = 0;
	atomic_set(&target_tgid, -1);
}

static void update_hmbird_II_policy_ilocked(struct cand_crit_node *cand_tmp,
	struct sched_prop_map sched_map, int filter_min_wake_cnt)
{
	if (NULL == cand_tmp) {
		return;
	}
	if (cand_tmp->waker_count + cand_tmp->wakee_count < filter_min_wake_cnt) {
		return;
	}
	if (cand_tmp->configed == CONFIGED) {
		return;
	} else if (cand_tmp->configed == NOT_CONFIG) {
		internal_cfg_task_sched_prop_handler(cand_tmp->pid, sched_map);
		cand_tmp->configed = CONFIGED;
	}
	return;
}

static bool inline is_target_same_group(struct task_struct *task)
{
	return (task->tgid == atomic_read(&target_tgid)) ? true : false;
}

/* cmp for sorting ctsks list, order by wake_count */
static int cand_crit_wake_cmp(void *priv, const struct list_head *a, const struct list_head *b)
{
	struct cand_crit_node *ia, *ib;

	ia = list_entry(a, struct cand_crit_node, list_node);
	ib = list_entry(b, struct cand_crit_node, list_node);

	if (ia->wakee_count + ia->waker_count > ib->wakee_count + ib->waker_count)
		return -1;
	else if (ia->wakee_count + ia->waker_count < ib->wakee_count + ib->waker_count)
		return 1;
	return 0;
}

static void add_to_cand_ilocked(struct list_head *cand_list_p, pid_t pid)
{
	struct cand_crit_node *new_node;
	if (task_node_cnt >= MAX_NODE_COUNT) {
		return;
	}
	new_node = kzalloc(sizeof(*new_node), GFP_KERNEL);
	if (!new_node) {
		CTM_INFO("no mem alloced\n");
		return;
	}
	init_cand_node(new_node, pid);
	list_add(&new_node->list_node, cand_list_p);
	task_node_cnt++;
#if CTM_INTERNAL_DEBUG
	CTM_INFO("wake scan task %d has set", pid);
#endif
}

static struct cand_crit_node *query_repeat_cand(struct list_head *head, pid_t target_pid) {
	struct cand_crit_node *entry;

	list_for_each_entry(entry, head, list_node) {
		if (entry->pid == target_pid) {
			return entry;
		}
	}
	return NULL;
}

static void find_sf_update_policy(void) {
	pid_t sf_pid_last = sf_search_prop.u_pid;
	pid_t sf_pid_new = -1;
	struct task_struct *task_sf;
	rcu_read_lock();
	task_sf = find_task_by_vpid(sf_pid_last);
	if (task_sf && 0 == strncmp(task_sf->comm, SF_NAME, strlen(SF_NAME))) {
		sf_search_prop.configed = NOT_CONFIG;
		rcu_read_unlock();
		return;
	}
	CTM_INFO("surfaceflinger not found\n");
	for_each_process(task_sf) {
		get_task_struct(task_sf);
		if (0 == strncmp(task_sf->comm, SF_NAME, strlen(SF_NAME))) {
			sf_pid_new = task_sf->pid;
			put_task_struct(task_sf);
			break;
		}
		put_task_struct(task_sf);
	}
	rcu_read_unlock();

	if (sf_pid_new > 0) {
		sf_search_prop.u_pid = sf_pid_new;
		internal_cfg_task_sched_prop_handler(sf_pid_new, sf_search_prop.sched_map);
		sf_search_prop.configed = CONFIGED;
		CTM_INFO("found new surfaceflinger %d\n", sf_pid_new);
	}
}

static void inline remove_cand_node_ilocked(struct cand_crit_node *cand_to_rm)
{
	if (!cand_to_rm)
		return;
	list_del(&cand_to_rm->list_node);
	kfree(cand_to_rm);
	task_node_cnt--;
}

static void remove_all_list_node_ilocked(void)
{
	int i;
	pid_t pid_tmp;
	struct cand_crit_node *cand_tmp, *cand_tmp_safe;

	for (i = 0; i < u_crit_cnt; i++) {
		list_for_each_entry_safe(cand_tmp, cand_tmp_safe, &u_crit_tasks[i].cand_list, list_node) {
			pid_tmp = cand_tmp->pid;
			remove_cand_node_ilocked(cand_tmp);
#if CTM_INTERNAL_DEBUG
			CTM_INFO("cand_node %d has been removed.\n", pid_tmp);
#endif
		}
	}
}

static void find_invalid_cand_tasks_ilocked(struct list_head *cand_list_p)
{
	struct task_struct *task = NULL;
	struct cand_crit_node *cand_tmp, *cand_tmp_safe;
	pid_t pid_tmp;

	list_for_each_entry_safe(cand_tmp, cand_tmp_safe, cand_list_p, list_node) {
		pid_tmp = cand_tmp->pid;
		task = find_task_by_vpid(pid_tmp);
		if (!task) {
			cand_tmp->configed = NEED_REMOVE;
#if CTM_INTERNAL_DEBUG
			CTM_INFO("cand_node %d will be removed later\n", pid_tmp);
#endif
		}
	}
}

static void remove_invalid_cand_ilocked(struct list_head *cand_list_p)
{
	struct cand_crit_node *cand_tmp, *cand_tmp_safe;
	pid_t pid_tmp;

	list_for_each_entry_safe(cand_tmp, cand_tmp_safe, cand_list_p, list_node) {
		pid_tmp = cand_tmp->pid;
		if (cand_tmp->configed == NEED_REMOVE) {
			remove_cand_node_ilocked(cand_tmp);
#if CTM_INTERNAL_DEBUG
			CTM_INFO("cand_node %d has been removed.\n", pid_tmp);
#endif
		}
	}
}

static void reset_wake_counts_ilocked(struct list_head *cand_list_p)
{
	struct cand_crit_node *cand_tmp;
	list_for_each_entry(cand_tmp, cand_list_p, list_node) {
		cand_tmp->wakee_count = 0;
		cand_tmp->waker_count = 0;
	}
}

static int init_u_crit_list_head(void)
{
	int i;
	for (i = 0; i < MAX_CRITICAL_NUM; i++) {
		INIT_LIST_HEAD(&u_crit_tasks[i].cand_list);
	}
	return 0;
}

#define MAX_CACHED_TASK 10
struct task_cache {
	pid_t pids[MAX_CACHED_TASK];
	int count;
};
static void cache_matching_task(struct task_struct *task,
                              struct task_cache caches[])
{
	int i;
	for (i = 0; i < u_crit_cnt; i++) {
		if (caches[i].count >= MAX_CACHED_TASK ||
			query_repeat_cand(&u_crit_tasks[i].cand_list, task->pid)) {
			continue;
		}
		if (!strncmp(u_crit_tasks[i].name, task->comm, strlen(u_crit_tasks[i].name))) {
			caches[i].pids[caches[i].count++] = task->pid;
		}
	}
}

static void process_cached_tasks(struct task_cache *caches)
{
	for (int i = 0; i < u_crit_cnt; i++) {
		remove_invalid_cand_ilocked(&u_crit_tasks[i].cand_list);
		for (int j = 0; j < caches[i].count; j++) {
			add_to_cand_ilocked(&u_crit_tasks[i].cand_list, caches[i].pids[j]);
		}
		list_sort(NULL, &u_crit_tasks[i].cand_list, cand_crit_wake_cmp);
	}
}

static void update_cand_first_policy(int filter_min_wake_cnt) {
	int i;
	struct cand_crit_node *cand_tmp;

	for (i = 0; i < u_crit_cnt; i++) {
		cand_tmp = list_first_entry_or_null(&u_crit_tasks[i].cand_list, struct cand_crit_node, list_node);
		update_hmbird_II_policy_ilocked(cand_tmp, u_crit_tasks[i].sched_map, MIN_WAKE_CNT);
		reset_wake_counts_ilocked(&u_crit_tasks[i].cand_list);
	}
}

static void ctm_timer_handler(struct timer_list *timer)
{
	int i;
	struct task_struct *root_task, *task;
	struct task_cache task_caches[MAX_CRITICAL_NUM] = {0};

	if (atomic_read(&g_ctm_enable)) {
		if (need_search_sf) {
			find_sf_update_policy();
		}
		/* search name matched task */
		rcu_read_lock();
		root_task = find_task_by_vpid(atomic_read(&target_tgid));
		if (!root_task) {
			rcu_read_unlock();
			ctm_switch(0);
			CTM_INFO("Target tgid not found, CTM exit.\n");
			return;
		}
		get_task_struct(root_task);
		for_each_thread(root_task, task) {
			get_task_struct(task);
			cache_matching_task(task, task_caches);
			put_task_struct(task);
		}
		put_task_struct(root_task);

		for (i = 0; i < u_crit_cnt; i++) {
			find_invalid_cand_tasks_ilocked(&u_crit_tasks[i].cand_list);
		}
		rcu_read_unlock();

		write_lock(&cand_task_rwlock);
		process_cached_tasks(task_caches);
		/* update policy */
		update_cand_first_policy(MIN_WAKE_CNT);
		write_unlock(&cand_task_rwlock);
	}
	mod_timer(&ctm_timer, jiffies + UTIL_INTERVAL_JIFF);
}

int ctm_set_finish_trigger(void)
{
	if (u_crit_cnt > 0 && 0 != atomic_read(&g_ctm_enable)) {
		ctm_timer_handler(&ctm_timer);
		update_cand_first_policy(0);
	}
	return 0;
}

/* set_ttwu_callback defined in oplus_bsp_waker_identify.ko */
static void ttwu_hmbird_entry(struct task_struct *task)
{
	int i;
	struct cand_crit_node *cand_tsk;

	if (0 == atomic_read(&g_ctm_enable))
		return;
	if (write_trylock(&cand_task_rwlock)) {
		for (i = 0; i < u_crit_cnt; i++) {
			cand_tsk = query_repeat_cand(&u_crit_tasks[i].cand_list, task->pid);
			if(NULL != cand_tsk) {
				cand_tsk->wakee_count++;
			}
			cand_tsk = query_repeat_cand(&u_crit_tasks[i].cand_list, current->pid);
			if(NULL != cand_tsk) {
				cand_tsk->waker_count++;
			}
		}
		write_unlock(&cand_task_rwlock);
	}
}

static void ttwu_noop_entry(struct task_struct *task)
{
	return;
}

extern int set_ttwu_callback(void (*entry_func)(struct task_struct *task));

int critical_task_monitor_init(void)
{
	ctm_reset_ilocked();
	init_u_crit_list_head();
	set_ttwu_callback(ttwu_hmbird_entry);
	timer_setup(&ctm_timer, ctm_timer_handler, 0);
	return 0;
}

int critical_task_monitor_deinit(void)
{
	set_ttwu_callback(ttwu_noop_entry);
	del_timer(&ctm_timer);
	return 0;
}

