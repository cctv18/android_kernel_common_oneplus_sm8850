/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Oplus. All rights reserved.
 */
#include <linux/kmemleak.h>
#include <linux/cpufreq.h>
#include <linux/sched.h>
#include <linux/sched/cputime.h>
#include <linux/mmu_context.h>
#include <../kernel/sched/sched.h>

#include "hmbird_II.h"
#include "hmbird_II_freqgov.h"

#define DEFAULT_TARGET_LOAD 90
#define DEFAULT_MUL_TARGET_LOAD "2;70:90:10"

#define MAX_MTL_STAGES		(1 << 5)
#define MAX_PERF_VAL		1024
#define MAX_CLUSTERS 4
static int gov_flag[MAX_CLUSTERS] = {0};
extern int nr_cluster;
extern struct hmbird_sched_cluster hmbird_cluster[MAX_NR_CLUSTER];
struct hmbird_gov_tunables {
	struct gov_attr_set     attr_set;
	unsigned int            target_loads;
	int                     soft_freq_max;
	int                     soft_freq_min;
	bool                    apply_freq_immediately;
	struct {
		int enable;
		int cnt;
		struct {
			unsigned int stage;
			unsigned int tl;
		} tls[MAX_MTL_STAGES];
		u16 tls_tab[MAX_PERF_VAL + 1];
	} mul_tl;
};

struct hmbird_gov_policy {
	struct cpufreq_policy	*policy;

	struct hmbird_gov_tunables	*tunables;
	struct list_head	tunables_hook;

	raw_spinlock_t		update_lock;	/* For shared policies */
	unsigned int		next_freq;
	unsigned int		next_raw_freq;
	/* The next fields are only needed if fast switch cannot be used: */
	struct kthread_work	work;
	struct mutex		work_lock;
	struct kthread_worker	worker;
	struct task_struct	*thread;
	bool			work_in_progress;
	unsigned int	target_load;
	bool		backup_efficiencies_available;
};

struct hmbird_gov_cpu {
	struct update_util_data	update_util;
	unsigned int		reasons;
	struct hmbird_gov_policy	*hg_policy;
	unsigned int		cpu;

	unsigned long		util;
	unsigned int		flags;
};

static DEFINE_PER_CPU(struct hmbird_gov_cpu, hmbird_gov_cpu);
static DEFINE_PER_CPU(struct hmbird_gov_tunables *, cached_tunables);
static DEFINE_MUTEX(global_tunables_lock);
static struct hmbird_gov_tunables *global_tunables;
bool hmbird_gov_heavy_boost_flag[MAX_CLUSTERS] = {false};

struct heavy_boost_params heavy_boost_param = {
	.type = 0,
	.bottom_perf = 400,
	.boost_weight = 140,
};

static void hmbird_gov_work(struct kthread_work *work)
{
	struct hmbird_gov_policy *hg_policy = container_of(work, struct hmbird_gov_policy, work);
	unsigned int freq;
	unsigned long flags;

	/*
	 * Hold hg_policy->update_lock shortly to handle the case where:
	 * incase hg_policy->next_freq is read here, and then updated by
	 * hmbird_gov_deferred_update() just before work_in_progress is set to false
	 * here, we may miss queueing the new update.
	 *
	 * Note: If a work was queued after the update_lock is released,
	 * hmbird_gov_work() will just be called again by kthread_work code; and the
	 * request will be proceed before the hmbird_gov thread sleeps.
	 */
	raw_spin_lock_irqsave(&hg_policy->update_lock, flags);
	freq = hg_policy->next_freq;
	raw_spin_unlock_irqrestore(&hg_policy->update_lock, flags);

	mutex_lock(&hg_policy->work_lock);
	__cpufreq_driver_target(hg_policy->policy, freq, CPUFREQ_RELATION_L);
	mutex_unlock(&hg_policy->work_lock);
}

#define DIV64_U64_ROUNDUP(X, Y) div64_u64((X) + (Y - 1), Y)

static inline unsigned int mtl_target_load(struct hmbird_gov_policy *hg_policy, u32 perf)
{
	unsigned int tl = DEFAULT_TARGET_LOAD;
	struct hmbird_gov_tunables *tunables = hg_policy->tunables;
	if (tunables) {
		if (!tunables->mul_tl.enable) {
			tl = tunables->target_loads;
		} else {
			if (unlikely(perf > MAX_PERF_VAL))
				perf = MAX_PERF_VAL;
			tl = tunables->mul_tl.tls_tab[perf];
		}
	}
	return tl;
}

static DEFINE_PER_CPU(int, prev_tl);
void target_load_state_systrace_c(unsigned int cpu, unsigned int tl)
{
	if (per_cpu(prev_tl, cpu) != tl) {
		hmbird_II_output_systrace("C|9999|Cpu%d_tl|%u\n", cpu, tl);
		per_cpu(prev_tl, cpu) = tl;
	}
}

static inline void hmbird_heavy_boost_systrace_c(int cpu, unsigned int next_f)
{
	hmbird_II_output_systrace("C|9999|Cpu%d_heavy_boost_freq|%u\n", cpu, next_f);
}

static inline void hmbird_top_task_boost_systrace_c(int cpu, unsigned int next_f)
{
	hmbird_II_output_systrace("C|9999|Cpu%d_top_task_boost_freq|%u\n",
			cpu, next_f);
}

static unsigned int get_next_freq(struct hmbird_gov_policy *hg_policy, u32 perf)
{
	struct cpufreq_policy *policy = hg_policy->policy;
	unsigned int freq = policy->cpuinfo.max_freq, next_f;
	unsigned int perf_tl, cluster_tl;
	int cpu = cpumask_first(policy->cpus);

	cluster_tl = mtl_target_load(hg_policy, perf);
	perf_tl = mult_frac(perf, 100, cluster_tl);
	next_f = mult_frac(freq, perf_tl, arch_scale_cpu_capacity(cpu));
	scx_trace_printk("cluster[%d] max_freq[%d] perf[%d] cpu_cap[%lu] cluster_tl[%u] perf_tl[%u] next_f[%d]\n",
			cpu, freq, perf, arch_scale_cpu_capacity(cpu), cluster_tl, perf_tl, next_f);
	if (unlikely(hmbird_debug & HMBIRD_DEBUG_SYSTRACE))
		target_load_state_systrace_c(policy->cpu, cluster_tl);

	return next_f;
}

static unsigned int soft_freq_clamp(struct hmbird_gov_policy *hg_policy, unsigned int target_freq)
{
	struct cpufreq_policy *policy = hg_policy->policy;
	int soft_freq_max = hg_policy->tunables->soft_freq_max;
	int soft_freq_min = hg_policy->tunables->soft_freq_min;

	if (soft_freq_min >= 0 && soft_freq_min > target_freq) {
		target_freq = soft_freq_min;
	}
	if (soft_freq_max >= 0 && soft_freq_max < target_freq) {
		target_freq = soft_freq_max;
	}

	scx_trace_printk("cluster[%d] max_freq[%d] min_freq[%d] freq[%d]\n",
			policy->cpu, soft_freq_max, soft_freq_min, target_freq);

	return target_freq;
}

void hmbird_gov_update_cpufreq(struct cpufreq_policy *policy, u32 perf)
{
	unsigned int next_f;
	struct hmbird_gov_policy *hg_policy = policy->governor_data;
	unsigned long irq_flags;

	raw_spin_lock_irqsave(&hg_policy->update_lock, irq_flags);

	next_f = get_next_freq(hg_policy, perf);
	hg_policy->next_raw_freq = next_f;
	next_f = soft_freq_clamp(hg_policy, next_f);
	next_f = cpufreq_driver_resolve_freq(policy, next_f);
	if (hg_policy->next_freq == next_f)
		goto unlock;
	hg_policy->next_freq = next_f;
	scx_trace_printk("cluster[%d] freq[%d] fast[%d]\n", policy->cpu, next_f, policy->fast_switch_enabled);
	if (policy->fast_switch_enabled)
		cpufreq_driver_fast_switch(policy, next_f);
	else
		kthread_queue_work(&hg_policy->worker, &hg_policy->work);

unlock:
	raw_spin_unlock_irqrestore(&hg_policy->update_lock, irq_flags);
}

void hmbird_gov_update_soft_limit_cpufreq(struct hmbird_gov_policy *hg_policy)
{
	unsigned int next_f;
	struct cpufreq_policy *policy = hg_policy->policy;
	unsigned long irq_flags;

	raw_spin_lock_irqsave(&hg_policy->update_lock, irq_flags);

	next_f = soft_freq_clamp(hg_policy, hg_policy->next_raw_freq);
	next_f = cpufreq_driver_resolve_freq(policy, next_f);
	if (hg_policy->next_freq == next_f)
		goto unlock;
	hg_policy->next_freq = next_f;
	scx_trace_printk("cluster[%d] freq[%d] fast[%d]\n",
			policy->cpu, next_f, policy->fast_switch_enabled);
	if (policy->fast_switch_enabled)
		cpufreq_driver_fast_switch(policy, next_f);
	else
		kthread_queue_work(&hg_policy->worker, &hg_policy->work);

unlock:
	raw_spin_unlock_irqrestore(&hg_policy->update_lock, irq_flags);
}

/************************** sysfs interface ************************/
static inline struct hmbird_gov_tunables *to_hmbird_gov_tunables(struct gov_attr_set *attr_set)
{
	return container_of(attr_set, struct hmbird_gov_tunables, attr_set);
}

static ssize_t mul_tl_show(struct gov_attr_set *attr_set, char *buf)
{
	struct hmbird_gov_tunables *tunables = to_hmbird_gov_tunables(attr_set);
	ssize_t len = 0;

	len += sprintf(buf + len, "%d;", !!tunables->mul_tl.enable);
	for (int i = 0; i < tunables->mul_tl.cnt; i++) {
		len += sprintf(buf + len, "%u:%u", tunables->mul_tl.tls[i].stage, tunables->mul_tl.tls[i].tl);
		if (i < tunables->mul_tl.cnt - 1) {
			len += sprintf(buf + len, ";");
		}
	}
	buf[len++] = '\n';
	buf[len] = '\0';
	return len;
}

static ssize_t mul_tl_store(struct gov_attr_set *attr_set, const char *buf, size_t count)
{
	struct hmbird_gov_tunables *tunables = to_hmbird_gov_tunables(attr_set);
	struct hmbird_gov_policy *hg_policy =
		list_first_entry(&attr_set->policy_list, struct hmbird_gov_policy, tunables_hook);
	int cpu = hg_policy->policy->cpu, i = 0, x, j;
	char *p = (char *)buf;
	char *token;
	unsigned int stage, tl, enable, prev_stage = 0, prev_tl = DEFAULT_TARGET_LOAD;
	unsigned int tl_min, tl_max, rate, cap = arch_scale_cpu_capacity(cpu);
	static const unsigned int f1 = 100, f2 = 10000;

	WRITE_ONCE(tunables->mul_tl.enable, 0);
	tunables->mul_tl.cnt = 0;
	memset(tunables->mul_tl.tls_tab, DEFAULT_TARGET_LOAD, MAX_PERF_VAL + 1);

	token = strsep(&p, ";");
	if (kstrtouint(token, 10, &enable) || (enable > 2)) {
		return -EINVAL;
	}

	if (enable == 1) {
		while ((token = strsep(&p, ";")) != NULL) {
			if (sscanf(token, "%u:%u", &stage, &tl) != 2) {
				return -EINVAL;
			}
			stage = mult_frac(stage, cap, 100);
			if (stage <= prev_stage || stage > MAX_PERF_VAL || tl > 100 || !tl)
				return -EINVAL;
			prev_stage = stage;
			prev_tl = tl;
			if (tunables->mul_tl.cnt < MAX_MTL_STAGES) {
				tunables->mul_tl.tls[tunables->mul_tl.cnt].stage = stage;
				tunables->mul_tl.tls[tunables->mul_tl.cnt].tl = tl;
				tunables->mul_tl.cnt++;
				for (; i <= MAX_PERF_VAL && i <= stage; i++)
					tunables->mul_tl.tls_tab[i] = tl;
			} else {
				return -ENOMEM;
			}
		}
		for (; i <= MAX_PERF_VAL; i++)
			tunables->mul_tl.tls_tab[i] = prev_tl;

		tunables->mul_tl.enable = enable;
	} else if (enable == 2) {
		if (sscanf(p, "%u:%u:%u", &tl_min, &tl_max, &rate) != 3)
			return -EINVAL;
		if (tl_min > 100 || tl_max > 100 || tl_min >= tl_max || !rate || rate > 100)
			return -EINVAL;
		/*
		* mul_tl Frequency Speed Curve:
		* y = tl_min + (tl_max - tl_min)(1 - 1/(0.01 * rate * x + 1))
		* y = (100 * tl_max - 10000 (tl_max - tl_min) / (rate * x + 100)) / 100
		* tl_min : is the minimum target load
		* tl_max : is the maximum target load
		* In the range between tl_min && tl_max
		* the rate value can be chosen between 1 and 100.
		* A larger rate value results in a faster increase in tl
		* and a smoother adjustment of frequency.
		*/
		for (j = 0; j < MAX_MTL_STAGES; j++) {
			x = mult_frac(j + 1, f1, MAX_MTL_STAGES);
			stage = mult_frac(x, cap, f1);
			tl = ((tl_max * f1) - (((tl_max - tl_min) * f2) / (rate * x + f1))) / f1;
			tl += 1;
			if (stage <= prev_stage || stage > MAX_PERF_VAL || tl > 100 || !tl)
				return -EINVAL;
			prev_tl = tl;
			prev_stage = stage;
			tunables->mul_tl.tls[tunables->mul_tl.cnt].stage = stage;
			tunables->mul_tl.tls[tunables->mul_tl.cnt].tl = tl;
			tunables->mul_tl.cnt++;
			for (; i <= MAX_PERF_VAL && i <= stage; i++)
				tunables->mul_tl.tls_tab[i] = tl;
		}
		for (; i <= MAX_PERF_VAL; i++)
			tunables->mul_tl.tls_tab[i] = prev_tl;
		tunables->mul_tl.enable = enable;
	}
	return count;
}


static ssize_t target_loads_show(struct gov_attr_set *attr_set, char *buf)
{
	struct hmbird_gov_tunables *tunables = to_hmbird_gov_tunables(attr_set);
	return sprintf(buf, "%d\n", tunables->target_loads);
}

static ssize_t target_loads_store(struct gov_attr_set *attr_set, const char *buf,
					size_t count)
{
	struct hmbird_gov_tunables *tunables = to_hmbird_gov_tunables(attr_set);
	unsigned int new_target_loads = DEFAULT_TARGET_LOAD;

	if (kstrtouint(buf, 10, &new_target_loads))
		return -EINVAL;

	tunables->target_loads = new_target_loads;

	return count;
}

static ssize_t soft_freq_max_show(struct gov_attr_set *attr_set, char *buf)
{
	struct hmbird_gov_tunables *tunables = to_hmbird_gov_tunables(attr_set);
	int soft_freq_max = tunables->soft_freq_max;

	if (soft_freq_max < 0) {
		return sprintf(buf, "max\n");
	} else {
		return sprintf(buf, "%d\n", soft_freq_max);
	}
}

static ssize_t soft_freq_max_store(struct gov_attr_set *attr_set, const char *buf, size_t count)
{
	struct hmbird_gov_tunables *tunables = to_hmbird_gov_tunables(attr_set);
	struct hmbird_gov_policy *hg_policy = list_first_entry(&attr_set->policy_list, struct hmbird_gov_policy, tunables_hook);
	int new_soft_freq_max = -1;

	if (kstrtoint(buf, 10, &new_soft_freq_max))
		return -EINVAL;

	if (tunables->soft_freq_max == new_soft_freq_max) {
		return count;
	}

	tunables->soft_freq_max = new_soft_freq_max;
	if (tunables->apply_freq_immediately) {
		hmbird_gov_update_soft_limit_cpufreq(hg_policy);
	}

	return count;
}

static ssize_t soft_freq_min_show(struct gov_attr_set *attr_set, char *buf)
{
	struct hmbird_gov_tunables *tunables = to_hmbird_gov_tunables(attr_set);
	int soft_freq_min = tunables->soft_freq_min;

	if (soft_freq_min < 0) {
		return sprintf(buf, "0\n");
	} else {
		return sprintf(buf, "%d\n", soft_freq_min);
	}
}

static ssize_t soft_freq_min_store(struct gov_attr_set *attr_set, const char *buf, size_t count)
{
	struct hmbird_gov_tunables *tunables = to_hmbird_gov_tunables(attr_set);
	struct hmbird_gov_policy *hg_policy = list_first_entry(&attr_set->policy_list, struct hmbird_gov_policy, tunables_hook);
	int new_soft_freq_min = -1;

	if (kstrtoint(buf, 10, &new_soft_freq_min))
		return -EINVAL;

	if (tunables->soft_freq_min == new_soft_freq_min) {
		return count;
	}

	tunables->soft_freq_min = new_soft_freq_min;
	if (tunables->apply_freq_immediately) {
		hmbird_gov_update_soft_limit_cpufreq(hg_policy);
	}

	return count;
}

static ssize_t soft_freq_cur_show(struct gov_attr_set *attr_set __maybe_unused, char *buf)
{
	return sprintf(buf, "none\n");
}

static ssize_t soft_freq_cur_store(struct gov_attr_set *attr_set, const char *buf, size_t count)
{
	struct hmbird_gov_tunables *tunables = to_hmbird_gov_tunables(attr_set);
	struct hmbird_gov_policy *hg_policy = list_first_entry(&attr_set->policy_list, struct hmbird_gov_policy, tunables_hook);
	int new_soft_freq_cur = -1;

	if (kstrtoint(buf, 10, &new_soft_freq_cur))
		return -EINVAL;

	if (tunables->soft_freq_max == new_soft_freq_cur && tunables->soft_freq_min == new_soft_freq_cur) {
		return count;
	}

	tunables->soft_freq_max = new_soft_freq_cur;
	tunables->soft_freq_min = new_soft_freq_cur;
	if (tunables->apply_freq_immediately) {
		hmbird_gov_update_soft_limit_cpufreq(hg_policy);
	}

	return count;
}

static ssize_t apply_freq_immediately_show(struct gov_attr_set *attr_set, char *buf)
{
	struct hmbird_gov_tunables *tunables = to_hmbird_gov_tunables(attr_set);
	return sprintf(buf, "%d\n", (int)tunables->apply_freq_immediately);
}

static ssize_t apply_freq_immediately_store(struct gov_attr_set *attr_set, const char *buf, size_t count)
{
	struct hmbird_gov_tunables *tunables = to_hmbird_gov_tunables(attr_set);
	int new_apply_freq_immediately = 0;

	if (kstrtoint(buf, 10, &new_apply_freq_immediately))
		return -EINVAL;

	tunables->apply_freq_immediately = new_apply_freq_immediately > 0;
	return count;
}

static struct governor_attr target_loads =
	__ATTR(target_loads, 0664, target_loads_show, target_loads_store);

static struct governor_attr soft_freq_max =
	__ATTR(soft_freq_max, 0664, soft_freq_max_show, soft_freq_max_store);

static struct governor_attr soft_freq_min =
	__ATTR(soft_freq_min, 0664, soft_freq_min_show, soft_freq_min_store);

static struct governor_attr soft_freq_cur =
	__ATTR(soft_freq_cur, 0664, soft_freq_cur_show, soft_freq_cur_store);

static struct governor_attr apply_freq_immediately =
	__ATTR(apply_freq_immediately, 0664, apply_freq_immediately_show, apply_freq_immediately_store);

static struct governor_attr mul_tl =
	__ATTR(mul_tl, 0664, mul_tl_show, mul_tl_store);

static struct attribute *hmbird_gov_attrs[] = {
	&target_loads.attr,
	&soft_freq_max.attr,
	&soft_freq_min.attr,
	&soft_freq_cur.attr,
	&apply_freq_immediately.attr,
	&mul_tl.attr,
	NULL
};
ATTRIBUTE_GROUPS(hmbird_gov);

static struct kobj_type hmbird_gov_tunables_ktype = {
	.default_groups = hmbird_gov_groups,
	.sysfs_ops = &governor_sysfs_ops,
};

/********************** cpufreq governor interface *********************/
struct cpufreq_governor cpufreq_hmbird_gov;

static struct hmbird_gov_policy *hmbird_gov_policy_alloc(struct cpufreq_policy *policy)
{
	struct hmbird_gov_policy *hg_policy;

	hg_policy = kzalloc(sizeof(*hg_policy), GFP_KERNEL);
	if (!hg_policy)
		return NULL;

	hg_policy->policy = policy;
	raw_spin_lock_init(&hg_policy->update_lock);
	return hg_policy;
}

static inline void hmbird_gov_cpu_reset(struct hmbird_gov_policy *hg_policy)
{
	unsigned int cpu;

	for_each_cpu(cpu, hg_policy->policy->cpus) {
		struct hmbird_gov_cpu *hg_cpu = &per_cpu(hmbird_gov_cpu, cpu);

		hg_cpu->hg_policy = NULL;
	}
}

static void hmbird_gov_policy_free(struct hmbird_gov_policy *hg_policy)
{
	kfree(hg_policy);
}

static inline void get_boost_cpuperf(struct rq *rq)
{
	if (rq->scx.cpuperf_target < heavy_boost_param.bottom_perf) {
		rq->scx.cpuperf_target = heavy_boost_param.bottom_perf;
	} else {
		rq->scx.cpuperf_target = mult_frac(rq->scx.cpuperf_target, heavy_boost_param.boost_weight, 100);
	}
}

static void hmbirdgov_update_freq(struct update_util_data *cb, u64 time, unsigned int flags)
{
	struct hmbird_gov_cpu *hg_cpu = container_of(cb, struct hmbird_gov_cpu, update_util);
	struct cpufreq_policy *policy = cpufreq_cpu_get_raw(hg_cpu->cpu);
	struct rq *rq = cpu_rq(hg_cpu->cpu);
	int cluster_id = topology_cluster_id(hg_cpu->cpu);
	unsigned int flag = rq->scx.cpuperf_target & HMBIRD_CPUFREQ_PERF_FLAG_MASK;

	if (flag == HMBIRD_CPUFREQ_WINDOW_ROLLOVER_FLAG || flag == HMBIRD_TASK_UCLAMP_UPDATE_FLAG
						|| flag == HMBIRD_CPUFREQ_CAMREA_BOOST_FLAG) {
		if (hmbird_gov_heavy_boost_flag[cluster_id]) {
			hmbird_gov_heavy_boost_flag[cluster_id] = false;
			if (unlikely(hmbird_debug & HMBIRD_DEBUG_SYSTRACE))
				hmbird_heavy_boost_systrace_c(policy->cpu, 0);
			goto clear;
		}
		hmbird_gov_update_cpufreq(policy, rq->scx.cpuperf_target | HMBIRD_CPUFREQ_PERF_FLAG_MASK);
	}
	if (flag == HMBIRD_CPUFREQ_HEAVY_BOOST_FLAG) {
		if (!(heavy_boost_param.type & HMBIRD_CPUFREQ_HEAVY_BOOST_FLAG))
			goto clear;
		get_boost_cpuperf(rq);
		hmbird_gov_heavy_boost_flag[cluster_id] = true;
		hmbird_gov_update_cpufreq(policy, rq->scx.cpuperf_target);
		if (unlikely(hmbird_debug & HMBIRD_DEBUG_SYSTRACE))
			hmbird_heavy_boost_systrace_c(policy->cpu, rq->scx.cpuperf_target);
	}
	if (flag == HMBIRD_CPUFREQ_TOP_TASK_BOOST_FLAG) {
		if (!(heavy_boost_param.type & HMBIRD_CPUFREQ_TOP_TASK_BOOST_FLAG))
			goto clear;
		get_boost_cpuperf(rq);
		hmbird_gov_update_cpufreq(policy, rq->scx.cpuperf_target);
		if (unlikely(hmbird_debug & HMBIRD_DEBUG_SYSTRACE))
			hmbird_top_task_boost_systrace_c(policy->cpu, rq->scx.cpuperf_target);
	}
clear:
	rq->scx.cpuperf_target = 0;
}

static int hmbird_gov_kthread_create(struct hmbird_gov_policy *hg_policy)
{
	struct task_struct *thread;
	struct cpufreq_policy *policy = hg_policy->policy;

	/* kthread only required for slow path */
	if (policy->fast_switch_enabled)
		return 0;

	kthread_init_work(&hg_policy->work, hmbird_gov_work);
	kthread_init_worker(&hg_policy->worker);
	thread = kthread_create(kthread_worker_fn, &hg_policy->worker,
				"hmbird_gov:%d",
				cpumask_first(policy->related_cpus));
	if (IS_ERR(thread)) {
		pr_err("failed to create hmbird_gov thread: %ld\n", PTR_ERR(thread));
		return PTR_ERR(thread);
	}

	/*
	 * if hmbird gov kthread work, set SCHED_DEADLINE
	 */

	hg_policy->thread = thread;
	kthread_bind_mask(thread, policy->related_cpus);
	mutex_init(&hg_policy->work_lock);

	wake_up_process(thread);

	return 0;
}

static void hmbird_gov_kthread_stop(struct hmbird_gov_policy *hg_policy)
{
	/* kthread only required for slow path */
	if (hg_policy->policy->fast_switch_enabled)
		return;

	kthread_flush_worker(&hg_policy->worker);
	kthread_stop(hg_policy->thread);
	mutex_destroy(&hg_policy->work_lock);
}

static struct hmbird_gov_tunables *hmbird_gov_tunables_alloc(struct hmbird_gov_policy *hg_policy)
{
	struct hmbird_gov_tunables *tunables;

	tunables = kzalloc(sizeof(*tunables), GFP_KERNEL);
	if (tunables) {
		gov_attr_set_init(&tunables->attr_set, &hg_policy->tunables_hook);
		if (!have_governor_per_policy())
			global_tunables = tunables;
	}
	return tunables;
}

static void hmbird_gov_tunables_free(struct hmbird_gov_tunables *tunables)
{
	if (!have_governor_per_policy())
		global_tunables = NULL;

	kfree(tunables);
}

#define DEFAULT_HISPEED_LOAD 90
static void hmbird_gov_tunables_save(struct cpufreq_policy *policy,
		struct hmbird_gov_tunables *tunables)
{
	int cpu;
	struct hmbird_gov_tunables *cached = per_cpu(cached_tunables, policy->cpu);

	if (!cached) {
		cached = kzalloc(sizeof(*tunables), GFP_KERNEL);
		if (!cached)
			return;

		for_each_cpu(cpu, policy->related_cpus)
			per_cpu(cached_tunables, cpu) = cached;
	}
}

/*********************************
 ***  hmbird cpufreq_governor  ***
 *********************************/

static int hmbird_gov_init(struct cpufreq_policy *policy)
{
	struct hmbird_gov_policy *hg_policy;
	struct hmbird_gov_tunables *tunables;
	int ret = 0;
	char *buf;

	/* State should be equivalent to EXIT */
	if (policy->governor_data)
		return -EBUSY;

	cpufreq_enable_fast_switch(policy);

	hg_policy = hmbird_gov_policy_alloc(policy);
	if (!hg_policy) {
		ret = -ENOMEM;
		goto disable_fast_switch;
	}

	ret = hmbird_gov_kthread_create(hg_policy);
	if (ret)
		goto free_hg_policy;

	mutex_lock(&global_tunables_lock);

	if (global_tunables) {
		if (WARN_ON(have_governor_per_policy())) {
			ret = -EINVAL;
			goto stop_kthread;
		}
		policy->governor_data = hg_policy;
		hg_policy->tunables = global_tunables;

		gov_attr_set_get(&global_tunables->attr_set, &hg_policy->tunables_hook);
		goto out;
	}

	tunables = hmbird_gov_tunables_alloc(hg_policy);
	if (!tunables) {
		ret = -ENOMEM;
		goto stop_kthread;
	}
	tunables->target_loads = DEFAULT_TARGET_LOAD;
	tunables->soft_freq_max = -1;
	tunables->soft_freq_min = -1;
	tunables->apply_freq_immediately = true;
	buf = kstrdup(DEFAULT_MUL_TARGET_LOAD, GFP_KERNEL);
	if (buf) {
		mul_tl_store(&tunables->attr_set, buf, 0);
		kfree(buf);
	}
	policy->governor_data = hg_policy;
	hg_policy->tunables = tunables;

	ret = kobject_init_and_add(&tunables->attr_set.kobj, &hmbird_gov_tunables_ktype,
					get_governor_parent_kobj(policy), "%s",
					cpufreq_hmbird_gov.name);
	if (ret)
		goto fail;

	policy->dvfs_possible_from_any_cpu = 1;

out:
	mutex_unlock(&global_tunables_lock);
	return 0;

fail:
	kobject_put(&tunables->attr_set.kobj);
	policy->governor_data = NULL;
	hmbird_gov_tunables_free(tunables);

stop_kthread:
	hmbird_gov_kthread_stop(hg_policy);
	mutex_unlock(&global_tunables_lock);

free_hg_policy:
	hmbird_gov_policy_free(hg_policy);

disable_fast_switch:
	cpufreq_disable_fast_switch(policy);

	pr_err("initialization failed (error %d)\n", ret);
	return ret;
}

static void hmbird_gov_exit(struct cpufreq_policy *policy)
{
	struct hmbird_gov_policy *hg_policy = policy->governor_data;
	struct hmbird_gov_tunables *tunables = hg_policy->tunables;
	unsigned int count;

	mutex_lock(&global_tunables_lock);

	count = gov_attr_set_put(&tunables->attr_set, &hg_policy->tunables_hook);
	policy->governor_data = NULL;
	if (!count) {
		hmbird_gov_tunables_save(policy, tunables);
		hmbird_gov_tunables_free(tunables);
	}

	mutex_unlock(&global_tunables_lock);

	hmbird_gov_kthread_stop(hg_policy);
	hmbird_gov_cpu_reset(hg_policy);
	hmbird_gov_policy_free(hg_policy);
	cpufreq_disable_fast_switch(policy);
}

void gov_switch_state_systrace_c(void)
{
	int state = 0;

	for (unsigned int i = 0; i < MAX_CLUSTERS; i++) {
		state |= gov_flag[i] << i;
	}
	hmbird_II_output_systrace("C|9999|hmbird_gov_state|%d\n", state);
}

static int hmbird_gov_start(struct cpufreq_policy *policy)
{
	struct hmbird_gov_policy *hg_policy = policy->governor_data;
	unsigned int cpu, cluster_id;

	hg_policy->next_freq = 0;

	for_each_cpu(cpu, policy->cpus) {
		struct hmbird_gov_cpu *hg_cpu = &per_cpu(hmbird_gov_cpu, cpu);

		memset(hg_cpu, 0, sizeof(*hg_cpu));
		hg_cpu->cpu			= cpu;
		hg_cpu->hg_policy		= hg_policy;
		cpufreq_add_update_util_hook(cpu, &hg_cpu->update_util, hmbirdgov_update_freq);
	}
	cpu = cpumask_first(policy->related_cpus);
	cluster_id = topology_cluster_id(cpu);
	pr_info("hmbird_II_freqgov: start cluster[%d] cluster_id[%d] gov\n", cpu, cluster_id);

	/* backup efficiencies_available, set scx efficiencies_available is false*/
	hg_policy->backup_efficiencies_available = policy->efficiencies_available;
	policy->efficiencies_available = false;
	if (cluster_id < MAX_CLUSTERS) {
		hmbird_gov_heavy_boost_flag[cluster_id] = false;
		gov_flag[cluster_id] = 1;
	}
	if (unlikely(hmbird_debug & HMBIRD_DEBUG_SYSTRACE))
		gov_switch_state_systrace_c();

	return 0;
}

static void hmbird_gov_stop(struct cpufreq_policy *policy)
{
	struct hmbird_gov_policy *hg_policy = policy->governor_data;
	unsigned int cpu, cluster_id;

	for_each_cpu(cpu, policy->cpus)
		cpufreq_remove_update_util_hook(cpu);

	if (!policy->fast_switch_enabled) {
		kthread_cancel_work_sync(&hg_policy->work);
	}

	/* restore efficiencies_available */
	policy->efficiencies_available = hg_policy->backup_efficiencies_available;

	cpu = cpumask_first(policy->related_cpus);
	cluster_id = topology_cluster_id(cpu);
	if (cluster_id < MAX_CLUSTERS)
		gov_flag[cluster_id] = 0;
	synchronize_rcu();
	pr_info("hmbird_II_freqgov: stop cluster[%d] cluster_id[%d] gov\n", cpu, cluster_id);
	if (unlikely(hmbird_debug & HMBIRD_DEBUG_SYSTRACE))
		gov_switch_state_systrace_c();
}

static void hmbird_gov_limits(struct cpufreq_policy *policy)
{
	struct hmbird_gov_policy *hg_policy = policy->governor_data;
	unsigned long flags, now;
	unsigned int freq, final_freq;

	if (!policy->fast_switch_enabled) {
		mutex_lock(&hg_policy->work_lock);
		cpufreq_policy_apply_limits(policy);
		mutex_unlock(&hg_policy->work_lock);
	} else {
		raw_spin_lock_irqsave(&hg_policy->update_lock, flags);

		freq = soft_freq_clamp(hg_policy, hg_policy->next_raw_freq);
		/*
		 * we have serval resources to update freq
		 * (1) scheduler to run callback
		 * (2) cpufreq_set_policy to call governor->limtis here
		 * so we have serveral times here and we must to keep them same
		 * here we using walt_sched_clock() to keep same with walt scheduler
		 */
		now = ktime_get_ns();

		/*
		 * cpufreq_driver_resolve_freq() has a clamp, so we do not need
		 * to do any sort of additional validation here.
		 */
		final_freq = cpufreq_driver_resolve_freq(policy, freq);
		cpufreq_driver_fast_switch(policy, final_freq);

		raw_spin_unlock_irqrestore(&hg_policy->update_lock, flags);
	}
}

struct cpufreq_governor cpufreq_hmbird_gov = {
	.name			= "hmbird",
	.owner			= THIS_MODULE,
	.flags			= CPUFREQ_GOV_DYNAMIC_SWITCHING,
	.init			= hmbird_gov_init,
	.exit			= hmbird_gov_exit,
	.start			= hmbird_gov_start,
	.stop			= hmbird_gov_stop,
	.limits			= hmbird_gov_limits,
};


int hmbird_freqgov_init(void)
{
	int ret = 0;
	struct hmbird_sched_cluster *cluster = NULL;

	ret = cpufreq_register_governor(&cpufreq_hmbird_gov);
	if (ret) {
		pr_err("failed to register governor\n");
		return ret;
	}

	for_each_sched_cluster(cluster)
		pr_info("num_cluster=%d id=%d cpumask=%*pbl capacity=%lu num_cpus=%d\n",
			nr_cluster, cluster->id, cpumask_pr_args(&cluster->cpus),
			arch_scale_cpu_capacity(cpumask_first(&cluster->cpus)),
			num_possible_cpus());

	return ret;
}
