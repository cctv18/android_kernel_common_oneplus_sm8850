// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Oplus. All rights reserved.
 */
#include <linux/slab.h>
#include <linux/sysctl.h>
#include <linux/kmemleak.h>
#include "hmbird_II.h"
#include "hmbird_II_shadow_tick.h"
#include "hmbird_II_freqgov.h"
#include "hmbird_II_sysctl.h"

#define CREATE_TRACE_POINTS
#include "trace_hmbird_II.h"

unsigned int hmbird_debug;
extern struct heavy_boost_params heavy_boost_param;

static int cfg_debug(const struct ctl_table *table,
				int write, void *buffer, size_t *lenp,
				loff_t *ppos)
{
	int ret = -EPERM;
	unsigned int val;
	static DEFINE_MUTEX(mutex);

	struct ctl_table tmp = {
		.data	= &val,
		.maxlen	= sizeof(val),
		.mode	= table->mode,
	};

	mutex_lock(&mutex);

	val = hmbird_debug;
	trace_hmbird_debug_update(val);
	ret = proc_dointvec(&tmp, write, buffer, lenp, ppos);
	if (ret) {
		HMBIRD_SYSCTL_WARN("get illegal hmbird_debug value\n");
		goto unlock;
	}
	if (!write || (val == hmbird_debug)) {
		goto unlock;
	}
	hmbird_debug = val;
	trace_hmbird_debug_update(val);
unlock:
	mutex_unlock(&mutex);
	return ret;
}

static unsigned long coefficient[MAX_NR_CLUSTER];
#define DEFAULT_COEFFCIENT 1024
#define MIN_COEFFCIENT 8
#define MAX_COEFFCIENT 4096
static int parse_coefficient_and_set(const char *input)
{
	char *token, *input_copy, *cur;
	int cluster_id = 0;
	int i;
	unsigned long coefficient_tmp[MAX_NR_CLUSTER];
	if (!input) {
		HMBIRD_SYSCTL_WARN("got nothing input\n");
		return -EINVAL;
	}

	input_copy = kstrdup(input, GFP_KERNEL);
	if (!input_copy) {
		HMBIRD_SYSCTL_WARN("kstrdup failed, no mem\n");
		return -ENOMEM;
	}

	cur = input_copy;

	while ((token = strsep(&cur, ",")) != NULL) {
		if (sscanf(token, "%lu", &coefficient_tmp[cluster_id]) != 1) {
			HMBIRD_SYSCTL_WARN("coefficient %s illegal\n", input_copy);
			kfree(input_copy);
			return -EINVAL;
		}
		if (coefficient_tmp[cluster_id] < MIN_COEFFCIENT
			|| coefficient_tmp[cluster_id] > MAX_COEFFCIENT) {
			HMBIRD_SYSCTL_WARN("coefficient value %lu out of range[%d, %d]\n",
				coefficient_tmp[cluster_id], MIN_COEFFCIENT, MAX_COEFFCIENT);
			kfree(input_copy);
			return -EINVAL;
		}
		if (++cluster_id >= nr_cluster)
			break;
	}
	if (cluster_id == nr_cluster) {
		for (i = 0; i < nr_cluster; i++) {
			coefficient[i] = coefficient_tmp[i];
			trace_hmbird_cfg_coefficient(i, coefficient[i]);
		}
	} else {
		HMBIRD_SYSCTL_WARN("coeff parms num err, input %d, clusters %d\n",
			cluster_id, nr_cluster);
	}

	kfree(input_copy);
	return 0;
}

static ssize_t print_coefficient(char *buf, size_t buf_size)
{
	int len = 0, cluster_id;
	for(cluster_id = 0; cluster_id < nr_cluster; cluster_id++) {
		trace_hmbird_cfg_coefficient(cluster_id, coefficient[cluster_id]);
		len += scnprintf(buf + len, buf_size - len, "%lu ", coefficient[cluster_id]);
	}
	return len;
}

unsigned long cfg_coefficient_get(int cluster_id)
{
	if (cluster_id < 0 && cluster_id >= MAX_NR_CLUSTER)
		return 0;
	return coefficient[cluster_id];
}
static int cfg_coefficient(const struct ctl_table *table,
				int write, void *buffer, size_t *lenp,
				loff_t *ppos)
{
	char input[128] = { };
	int ret = -EPERM;
	static DEFINE_MUTEX(mutex);
	struct ctl_table tmp = {
		.data	= &input,
		.maxlen	= sizeof(input),
	};
	mutex_lock(&mutex);
	if (write) {
		ret = proc_dostring(&tmp, write, buffer, lenp, ppos);
		if (ret) {
			ret = -EFAULT;
			HMBIRD_SYSCTL_WARN("write coefficient failed, input=%s\n", input);
			goto unlock;
		}
		ret = parse_coefficient_and_set(input);
	} else {
		ret = print_coefficient(input, sizeof(input));
		if (ret < 0) {
			ret = -EFAULT;
			HMBIRD_SYSCTL_WARN("read coefficient failed, input=%s\n", input);
			goto unlock;
		}
		ret = proc_dostring(&tmp, write, buffer, lenp, ppos);
	}
unlock:
	mutex_unlock(&mutex);
	return ret;
}

static unsigned long perf_high_ratio[MAX_NR_CLUSTER];
#define DEFAULT_PERF_H_RATIO	60
static int parse_perf_h_and_set(const char *input)
{
	char *token, *input_copy, *cur;
	int cluster_id = 0;
	if (!input) {
		HMBIRD_SYSCTL_WARN("input is empty\n");
		return -EINVAL;
	}

	input_copy = kstrdup(input, GFP_KERNEL);
	if (!input_copy)
		return -ENOMEM;

	cur = input_copy;

	while ((token = strsep(&cur, ",")) != NULL) {
		if (sscanf(token, "%lu", &perf_high_ratio[cluster_id]) != 1) {
			HMBIRD_SYSCTL_WARN("%s\n", input_copy);
			kfree(input_copy);
			return -EINVAL;
		}
		trace_hmbird_cfg_perf_high_ratio(cluster_id, perf_high_ratio[cluster_id]);
		if (++cluster_id >= nr_cluster)
			break;
	}

	kfree(input_copy);
	return 0;
}

static ssize_t print_perf_high_ratio(char *buf, size_t buf_size)
{
	int len = 0, cluster_id;
	for(cluster_id = 0; cluster_id < nr_cluster; cluster_id++) {
		trace_hmbird_cfg_perf_high_ratio(cluster_id, perf_high_ratio[cluster_id]);
		len += scnprintf(buf + len, buf_size - len, "%lu ", perf_high_ratio[cluster_id]);
	}
	return len;
}

unsigned long cfg_perf_high_ratio_get(int cluster_id)
{
	if (cluster_id < 0 && cluster_id >= MAX_NR_CLUSTER)
		return 0;
	return perf_high_ratio[cluster_id];
}
static int cfg_perf_high_ratio(const struct ctl_table *table,
				int write, void *buffer, size_t *lenp,
				loff_t *ppos)
{
	char input[128] = { };
	int ret = -EPERM;
	static DEFINE_MUTEX(mutex);
	struct ctl_table tmp = {
		.data	= &input,
		.maxlen	= sizeof(input),
	};
	mutex_lock(&mutex);
	if (write) {
		ret = proc_dostring(&tmp, write, buffer, lenp, ppos);
		if (ret) {
			ret = -EFAULT;
			HMBIRD_SYSCTL_WARN("high_ratio not set, ret %d\n", ret);
			goto unlock;
		}
		ret = parse_perf_h_and_set(input);
	} else {
		ret = print_perf_high_ratio(input, sizeof(input));
		if (ret < 0) {
			ret = -EFAULT;
			HMBIRD_SYSCTL_WARN("get high_ratio failed, ret %d\n", ret);
			goto unlock;
		}
		ret = proc_dostring(&tmp, write, buffer, lenp, ppos);
	}
unlock:
	mutex_unlock(&mutex);
	return ret;
}

static unsigned long freq_policy[MAX_NR_CLUSTER];
#define DEFAULT_FREQ_POLICY 0
static int parse_freq_policy_and_set(const char *input)
{
	char *token, *input_copy, *cur;
	int cluster_id = 0;
	if (!input)
		return -EINVAL;

	input_copy = kstrdup(input, GFP_KERNEL);
	if (!input_copy)
		return -ENOMEM;

	cur = input_copy;

	while ((token = strsep(&cur, ",")) != NULL) {
		if (sscanf(token, "%lu", &freq_policy[cluster_id]) != 1) {
			kfree(input_copy);
			return -EINVAL;
		}
		trace_hmbird_cfg_freq_policy(cluster_id, freq_policy[cluster_id]);
		if (++cluster_id >= nr_cluster)
			break;
	}

	kfree(input_copy);
	return 0;
}

static ssize_t print_freq_policy(char *buf, size_t buf_size)
{
	int len = 0, cluster_id;
	for(cluster_id = 0; cluster_id < nr_cluster; cluster_id++) {
		trace_hmbird_cfg_freq_policy(cluster_id, freq_policy[cluster_id]);
		len += scnprintf(buf + len, buf_size - len, "%lu ", freq_policy[cluster_id]);
	}
	return len;
}

unsigned long cfg_freq_policy_get(int cluster_id)
{
	if (cluster_id < 0 && cluster_id >= MAX_NR_CLUSTER)
		return 0;
	return freq_policy[cluster_id];
}

static int cfg_freq_policy(const struct ctl_table *table,
				int write, void *buffer, size_t *lenp,
				loff_t *ppos)
{
	char input[128] = { };
	int ret = -EPERM;
	static DEFINE_MUTEX(mutex);
	struct ctl_table tmp = {
		.data	= &input,
		.maxlen	= sizeof(input),
	};
	mutex_lock(&mutex);
	if (write) {
		ret = proc_dostring(&tmp, write, buffer, lenp, ppos);
		if (ret) {
			ret = -EFAULT;
			goto unlock;
		}
		ret = parse_freq_policy_and_set(input);
	} else {
		ret = print_freq_policy(input, sizeof(input));
		if (ret < 0) {
			ret = -EFAULT;
			goto unlock;
		}
		ret = proc_dostring(&tmp, write, buffer, lenp, ppos);
	}
unlock:
	mutex_unlock(&mutex);
	return ret;
}

static unsigned long cpus_reserved = 0xf0;
static unsigned long cpus_exclusive = 0x0;
static DEFINE_RAW_SPINLOCK(cfg_cpus_lock);
void cfg_cpus_get(cpumask_t *exclusive, cpumask_t *reserved)
{
	raw_spin_lock(&cfg_cpus_lock);
	exclusive->bits[0] = cpus_exclusive;
	reserved->bits[0] = cpus_reserved;
	raw_spin_unlock(&cfg_cpus_lock);
}

static int cfg_cpus(const struct ctl_table *table,
				int write, void *buffer, size_t *lenp,
				loff_t *ppos)
{
	int ret = 0;
	char buf[32];
	unsigned long *data = (unsigned long *)table->data;
	unsigned long old_val = *data, val;
	struct ctl_table tmp = { };

	raw_spin_lock(&cfg_cpus_lock);
	if (!write) {
		if (data == &cpus_reserved) {
			ret = scnprintf(buf, sizeof(buf), "cpus_reserved=0x%lx", old_val);
		} else {
			ret = scnprintf(buf, sizeof(buf), "cpus_exclusive=0x%lx", old_val);
		}
		if (ret < 0) {
			ret = -EFAULT;
			goto unlock;
		}
		tmp.data = &buf;
		tmp.maxlen = sizeof(buf);
		ret = proc_dostring(&tmp, write, buffer, lenp, ppos);
	} else {
		tmp.data = &val;
		tmp.maxlen = sizeof(val);
		ret = proc_dointvec(&tmp, write, buffer, lenp, ppos);
		if (ret || val == old_val) {
			HMBIRD_SYSCTL_WARN("cfg_cpus val %lx not set, ret %d\n", val, ret);
			goto unlock;
		}
		*data = val;
		if (cpus_reserved & cpus_exclusive) {
			HMBIRD_SYSCTL_WARN("cpus reserved %lx and exclusive %lx might conflict.\n",
			cpus_reserved, cpus_exclusive);
		}
	}
unlock:
	raw_spin_unlock(&cfg_cpus_lock);
	return ret;
}

static int frame_per_sec = 120;
int cfg_frame_per_sec_get(void)
{
	return frame_per_sec;
}
static int cfg_frame_per_sec(const struct ctl_table *table,
				int write, void *buffer, size_t *lenp,
				loff_t *ppos)
{
	int ret = -EPERM;
	int val;
	static DEFINE_MUTEX(mutex);

	struct ctl_table tmp = {
		.data = &val,
		.maxlen = sizeof(val),
		.mode = table->mode,
	};

	mutex_lock(&mutex);
	if (!write) {
		val = frame_per_sec;
		if (val <= 0 || val > 480)
			goto unlock;
		trace_hmbird_frame_update((unsigned int)val);
		ret = proc_dointvec(&tmp, write, buffer, lenp, ppos);
	} else {
		ret = proc_dointvec(&tmp, write, buffer, lenp, ppos);
		if (ret || val == frame_per_sec || val <= 0 || val > 480) {
			HMBIRD_SYSCTL_WARN("frame_per_sec val %d not set, ret %d\n", val, ret);
			goto unlock;
		}
		frame_per_sec = val;
		trace_hmbird_frame_update((unsigned int)val);
	}
unlock:
	mutex_unlock(&mutex);
	return ret;
}

#define CFG_CACHED_MAX		8


static struct task_struct *find_task_by_name(const char *name)
{
	struct task_struct *task;

	for_each_process(task) {
		if (strcmp(task->comm, name) == 0) {
			return task;
		}
	}
	return NULL;
}

struct {
	int idx;
	int cached[CFG_CACHED_MAX][2];
} cfg_cached[MAX_NR_CFG_TYPE];

enum media_scene_boost_type {
	HEAVY_TASK_BOOST,
	MAX_NR_BOOST_TYPE
};

enum cfg_critical_task_type {
	CRITICAL_TASK,
	MAX_CTM_TYPE
};

static int media_scene_boost_handler(const struct ctl_table *table,
				int write, void *buffer, size_t *lenp, loff_t *ppos)
{
	int ret = 0;
	enum media_scene_boost_type boost_type = (unsigned long)table->data;
	char input_buf[64];
	char buf[128];
	int type_tmp, bottom_perf_tmp, boost_weight_tmp;
	struct ctl_table tmp = { };
	if (boost_type != HEAVY_TASK_BOOST)
		return -EINVAL;
	if (!write) {
		ret = scnprintf(buf, sizeof(buf), "type=%d, bottom_perf=%d, boost_weight=%d", heavy_boost_param.type,
							heavy_boost_param.bottom_perf, heavy_boost_param.boost_weight);
		if (ret < 0) {
			return -EINVAL;
		}
		tmp.data = &buf;
		tmp.maxlen = sizeof(buf);
		ret = proc_dostring(&tmp, write, buffer, lenp, ppos);
	} else {
		strncpy(input_buf, (char *)buffer, sizeof(input_buf) - 1);
		input_buf[sizeof(input_buf) - 1] = '\0';
		if (sscanf(input_buf, "%d %d %d", &type_tmp, &bottom_perf_tmp, &boost_weight_tmp) != 3) {
			return -EINVAL;
		}
		heavy_boost_param.type = type_tmp;
		heavy_boost_param.bottom_perf = bottom_perf_tmp;
		heavy_boost_param.boost_weight = boost_weight_tmp;
	}
	return ret;
}

static DEFINE_MUTEX(cfg_sched_prop_mutex);
static int cfg_task_sched_prop_handler(const struct ctl_table *table,
				int write, void *buffer, size_t *lenp,
				loff_t *ppos)
{
	int ret = 0, val, i, idx, hex = false;
	int pid_and_val[2] = {-1, -1};
	char buf[512];
	char input_buf[64];
	char thread_name[TASK_COMM_LEN] = "";
	struct task_struct *task;
	struct ctl_table tmp = { };
	enum cfg_task_sched_prop cfg_type = (unsigned long)table->data;
	hex = cfg_type == PREFER_CPU || cfg_type == PREFER_CLUSTER;
	mutex_lock(&cfg_sched_prop_mutex);
	if (!write) {
		ret = snprintf(buf, sizeof(buf), "%s:\n", table->procname);
		if (ret < 0) {
			HMBIRD_SYSCTL_WARN("msg(%s) parse error\n", buf);
			goto unlock;
		}
		for (i = 0; i < CFG_CACHED_MAX; i++) {
			idx = (cfg_cached[cfg_type].idx + i) % CFG_CACHED_MAX;
			if (cfg_cached[cfg_type].cached[idx][0] != -1) {
				task = get_pid_task(find_vpid(cfg_cached[cfg_type].cached[idx][0]), PIDTYPE_PID);
				if (!task)
					continue;
				ret += snprintf(buf + ret, sizeof(buf) - ret, hex ? "%s::%s::%x\n" : "%s::%s::%d\n",
								task->group_leader->comm, task->comm, cfg_cached[cfg_type].cached[idx][1]);
				put_task_struct(task);
			}
		}
		tmp.data = &buf;
		tmp.maxlen = sizeof(buf);
		ret = proc_dostring(&tmp, write, buffer, lenp, ppos);
	} else {
		strncpy(input_buf, (char *)buffer, sizeof(input_buf) - 1);
		input_buf[sizeof(input_buf) - 1] = '\0';
		if (sscanf(input_buf, hex ? "%d %x" : "%d %d", &pid_and_val[0], &pid_and_val[1]) == 2) {
			if (pid_and_val[0] <= 0) {
				ret = -ESRCH;
				HMBIRD_SYSCTL_WARN("illegal pid %d\n", pid_and_val[0]);
				goto unlock;
			}
			task = get_pid_task(find_vpid(pid_and_val[0]), PIDTYPE_PID);
			if (!task) {
				ret = -ESRCH;
				HMBIRD_SYSCTL_WARN("task with pid %d not found\n", pid_and_val[0]);
				goto unlock;
			}
		} else {
			if (sscanf(input_buf, "%s %d", thread_name, &val) == 2) {
				task = find_task_by_name(thread_name);
				if (!task) {
					ret = -ESRCH;
					HMBIRD_SYSCTL_WARN("task %s not found\n", thread_name);
					goto unlock;
				}
				pid_and_val[0] = task->pid;
				pid_and_val[1] = val;
			} else {
				ret = -EINVAL;
				HMBIRD_SYSCTL_WARN("illegal input=%s\n", input_buf);
				goto unlock;
			}
		}

		switch (cfg_type) {
		case PREFER_IDLE:
			if (pid_and_val[1] == 1)
				hmbird_sched_prop_set_prefer_idle(task, 1);
			else if (pid_and_val[1] == 0)
				hmbird_sched_prop_set_prefer_idle(task, 0);
			else {
				ret = -EINVAL;
				HMBIRD_SYSCTL_WARN("illegal value %d\n", pid_and_val[1]);
			}
			break;
		case PREFER_PREEMPT:
			if (pid_and_val[1] == 1)
				hmbird_sched_prop_set_prefer_preempt(task, 1);
			else if (pid_and_val[1] == 0)
				hmbird_sched_prop_set_prefer_preempt(task, 0);
			else {
				ret = -EINVAL;
				HMBIRD_SYSCTL_WARN("illegal value %d\n", pid_and_val[1]);
			}
			break;
		case PREFER_CLUSTER:
			if (pid_and_val[1] < 0 || pid_and_val[1] > ((1 << nr_cluster) - 1)) {
				ret = -EINVAL;
				HMBIRD_SYSCTL_WARN("illegal value %d\n", pid_and_val[1]);
			}
			else if (pid_and_val[1])
				hmbird_sched_prop_set_prefer_cluster(task, pid_and_val[1], 1);
			else
				hmbird_sched_prop_set_prefer_cluster(task, 0, 0);
			break;
		case PREFER_CPU:
			if (pid_and_val[1] < 0 || pid_and_val[1] > 0xff) {
				ret = -EINVAL;
				HMBIRD_SYSCTL_WARN("illegal value %d\n", pid_and_val[1]);
			}
			else if (pid_and_val[1])
				hmbird_sched_prop_set_prefer_cpu(task, pid_and_val[1], 1);
			else
				hmbird_sched_prop_set_prefer_cpu(task, 0, 0);
			break;
		case SET_UCLAMP:
			if (pid_and_val[1] > 0 && pid_and_val[1] <= 1024)
					hmbird_sched_prop_set_uclamp(task, pid_and_val[1]);
			else if (pid_and_val[1] == 0)
				hmbird_sched_prop_set_uclamp(task, 0);
			else {
				ret = -EINVAL;
				HMBIRD_SYSCTL_WARN("illegal value %d\n", pid_and_val[1]);
			}
			break;
		case UCLAMP_KEEP_FREQ:
			if (pid_and_val[1] == 1)
				hmbird_sched_prop_uclamp_keep_freq(task, 1);
			else if (pid_and_val[1] == 0)
				hmbird_sched_prop_uclamp_keep_freq(task, 0);
			else {
				ret = -EINVAL;
				HMBIRD_SYSCTL_WARN("illegal value %d\n", pid_and_val[1]);
			}
			break;
		default:
			ret = -EINVAL;
			HMBIRD_SYSCTL_WARN("no matched cfg_type %d\n", cfg_type);
		}
		if (!ret) {
			idx = cfg_cached[cfg_type].idx;
			cfg_cached[cfg_type].cached[idx][0] = task->pid;
			cfg_cached[cfg_type].cached[idx][1] = pid_and_val[1];
			cfg_cached[cfg_type].idx = (idx + 1) % CFG_CACHED_MAX;
		}
		put_task_struct(task);
	}

unlock:
	mutex_unlock(&cfg_sched_prop_mutex);
	return ret;
}

/* for internal config sched_prop */
int internal_cfg_task_sched_prop_handler(pid_t pid, struct sched_prop_map sched_map)
{
	int ret = 0, idx;
	struct task_struct *task;

	mutex_lock(&cfg_sched_prop_mutex);

	task = get_pid_task(find_vpid(pid), PIDTYPE_PID);
	if (!task) {
		ret = -ESRCH;
		goto unlock;
	}
	hmbird_sched_prop_set_sched_prop_directly(task, sched_map.sched_prop, sched_map.sched_prop_mask);
	/* need to handle u64 sched_prop to int type cached value case, 999 instead now */
	if (!ret) {
		idx = cfg_cached[SCHED_PROP_DIRECTLY].idx;
		cfg_cached[SCHED_PROP_DIRECTLY].cached[idx][0] = task->pid;
		cfg_cached[SCHED_PROP_DIRECTLY].cached[idx][1] = 999;
		cfg_cached[SCHED_PROP_DIRECTLY].idx = (idx + 1) % CFG_CACHED_MAX;
	}
	put_task_struct(task);

unlock:
	mutex_unlock(&cfg_sched_prop_mutex);
	return ret;
}

extern int set_critical_task(struct critical_task_params *params);
extern int ctm_set_finish_trigger(void);
extern int get_critical_task_list(char *result_buf, int buf_size);

/* handle the critical input, only care the params nums, not care about the params meanings */
static int cfg_critical_task_list_handler(const struct ctl_table *table,
				int write, void *buffer, size_t *lenp,
				loff_t *ppos)
{
	int ret = 0;
	char buf[RESULT_PAGE_SIZE];
	struct critical_task_params crit_tsk_param;
	char *token, *input_copy, *cur;

	struct ctl_table tmp = { };
	static DEFINE_MUTEX(critical_task_mutex);
	mutex_lock(&critical_task_mutex);
	if (!write) {
		ret = snprintf(buf, sizeof(buf), "%s:\n", table->procname);
		if (ret < 0)
			goto unlock;
		get_critical_task_list(buf, sizeof(buf));
		tmp.data = &buf;
		tmp.maxlen = sizeof(buf);
		ret = proc_dostring(&tmp, write, buffer, lenp, ppos);
		goto unlock;
	} else {
		if (strlen(buffer) >= RESULT_PAGE_SIZE) {
			pr_warn("Input too long, max %d chars allowed\n", RESULT_PAGE_SIZE);
			ret = -EINVAL;
			goto unlock;
		}
		input_copy = kstrdup(buffer, GFP_KERNEL);
		if (!input_copy) {
			pr_warn("Failed to allocate memory for input copy\n");
			ret = -ENOMEM;
			goto unlock;
		}
		cur = input_copy;
		while ((token = strsep(&cur, "#")) != NULL && *token != '\0') {
			crit_tsk_param.param_nums = sscanf(token, "%d %d %s %llx %llx",
				&crit_tsk_param.type,
				&crit_tsk_param.pid,
				crit_tsk_param.name,
				&crit_tsk_param.sched_map.sched_prop,
				&crit_tsk_param.sched_map.sched_prop_mask);
			set_critical_task(&crit_tsk_param);
		}
		ctm_set_finish_trigger();
		ret = 0;
		goto unlock;
	}
unlock:
	mutex_unlock(&critical_task_mutex);
	return ret;
}


struct ctl_table hmbird_table[] = {
	{
		.procname	= "hmbird_debug",
		.data		= &hmbird_debug,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0666,
		.proc_handler	= cfg_debug,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_INT_MAX,
	},
	{
		.procname	= "highres_tick_enable",
		.data		= &highres_tick_ctrl,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_INT_MAX,
	},
	{
		.procname	= "highres_tick_debug",
		.data		= &highres_tick_ctrl_dbg,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_INT_MAX,
	},
	{
		.procname	= "coefficient",
		.data		= &coefficient,
		.maxlen		= 128,
		.mode		= 0644,
		.proc_handler	= cfg_coefficient,
	},
	{
		.procname	= "perf_high_ratio",
		.data		= &perf_high_ratio,
		.maxlen		= 128,
		.mode		= 0644,
		.proc_handler	= cfg_perf_high_ratio,
	},
	{
		.procname	= "freq_policy",
		.data		= &freq_policy,
		.maxlen		= 128,
		.mode		= 0644,
		.proc_handler	= cfg_freq_policy,
	},
	{
		.procname	= "cpus_reserved",
		.data		= &cpus_reserved,
		.maxlen		= sizeof(unsigned long),
		.mode		= 0644,
		.proc_handler	= cfg_cpus,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_INT_MAX,
	},
	{
		.procname	= "cpus_exclusive",
		.data		= &cpus_exclusive,
		.maxlen		= sizeof(unsigned long),
		.mode		= 0644,
		.proc_handler	= cfg_cpus,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_INT_MAX,
	},
	{
		.procname	= "frame_per_sec",
		.data		= &frame_per_sec,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= cfg_frame_per_sec,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_INT_MAX,
	},
	{
		.procname	= "prefer_idle",
		.data		= (int *)PREFER_IDLE,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= cfg_task_sched_prop_handler,
	},
	{
		.procname	= "prefer_preempt",
		.data		= (int *)PREFER_PREEMPT,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= cfg_task_sched_prop_handler,
	},
	{
		.procname	= "prefer_cluster",
		.data		= (int *)PREFER_CLUSTER,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= cfg_task_sched_prop_handler,
	},
	{
		.procname	= "prefer_cpu",
		.data		= (int *)PREFER_CPU,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= cfg_task_sched_prop_handler,
	},
	{
		.procname	= "set_uclamp",
		.data		= (int *)SET_UCLAMP,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= cfg_task_sched_prop_handler,
	},
	{
		.procname	= "uclamp_keep_freq",
		.data		= (int *)UCLAMP_KEEP_FREQ,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= cfg_task_sched_prop_handler,
	},
	{
		.procname	= "heavy_boost",
		.data		= (int *)HEAVY_TASK_BOOST,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= media_scene_boost_handler,
	},
	{
		.procname	= "critical_task",
		.data		= (int *)CRITICAL_TASK,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= cfg_critical_task_list_handler,
	},
};

static struct ctl_table_header *hdr;
int hmbird_sysctl_init(void)
{
	int i;
	for (i = 0; i < MAX_NR_CLUSTER; i++) {
		coefficient[i] = DEFAULT_COEFFCIENT;
		perf_high_ratio[i] = DEFAULT_PERF_H_RATIO;
		freq_policy[i] = DEFAULT_FREQ_POLICY;
	}
	hdr = register_sysctl("hmbird_II", hmbird_table);

	kmemleak_not_leak(hdr);
	return 0;
}

void hmbird_sysctl_deinit(void)
{
	unregister_sysctl_table(hdr);
}
