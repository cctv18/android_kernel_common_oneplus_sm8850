/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Oplus. All rights reserved.
 */
#ifndef _HMBIRD_II_H_
#define _HMBIRD_II_H_
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include "hmbird_II_bpf.h"
#include "hmbird_common.h"
#define HMBIRD_DEBUG_FTRACE		(1 << 0)
#define HMBIRD_DEBUG_SYSTRACE		(1 << 1)
#define HMBIRD_DEBUG_PRINTK		(1 << 2)
#define HMBIRD_DEBUG_PANIC		(1 << 3)
#define HMBIRD_II_SCENE 	2
#define MULTIMEDIA_SCENE	HMBIRD_II_SCENE
#define HMBIRD_III_SCENE	3
extern unsigned int hmbird_debug;
extern unsigned int hmbird_enable;

#define HMBIRD_ERR(fmt, ...)	\
{				\
	pr_err("hmbird_II_err[%s][%d]: "fmt, __func__, __LINE__, ##__VA_ARGS__);	\
}

#define HMBIRD_SYSCTL_WARN(fmt, ...)	\
{				\
	pr_warn("hmbird_II_sysctl get bad args: [%s][%d]: "fmt, __func__, __LINE__, ##__VA_ARGS__);	\
}

#define scx_trace_printk(fmt, ...)				\
{								\
	if (unlikely(hmbird_debug & HMBIRD_DEBUG_FTRACE))			\
		trace_printk("hmbird_II:"fmt, ##__VA_ARGS__);	\
}

#define scx_trace_printk_kfunc(fmt, ...)			\
{								\
	if (unlikely(hmbird_debug & HMBIRD_DEBUG_FTRACE))			\
		trace_printk("hmbird_II_kfunc:"fmt, ##__VA_ARGS__);\
}

#define hmbird_II_output_systrace(fmt, ...)	\
do {					\
	char buf[256];		\
	snprintf(buf, sizeof(buf), fmt, ##__VA_ARGS__);	\
	tracing_mark_write(buf);			\
} while (0)


#define hmbird_II_jank_systrace(fmt, ...)			\
do {								\
	if (unlikely(hmbird_debug & HMBIRD_DEBUG_SYSTRACE)) {	\
		hmbird_II_output_systrace(fmt, ##__VA_ARGS__);	\
	}							\
} while (0)

struct hmbird_sched_cluster {
	struct cpumask		cpus;
	int 			id;
};
#define INVAILD_CLUSTER_ID 	-1
#define MAX_NR_CLUSTER		6
#define MAX_NR_CPUS			8
extern int nr_cluster;
extern struct hmbird_sched_cluster hmbird_cluster[];

#define for_each_sched_cluster(cluster)	\
	for(cluster = &hmbird_cluster[0];	\
		cluster->id < nr_cluster; cluster = &hmbird_cluster[cluster->id + 1])

static inline struct hmbird_sched_cluster *get_hmbird_sched_cluster(int cluster_id)
{
	if (cluster_id < 0 || cluster_id >= nr_cluster)
		return NULL;

	return &hmbird_cluster[cluster_id];
}

noinline int tracing_mark_write(const char *buf);

#define LOOKUP_KERNEL_SYMBOL(name) \
static int lookup_##name(void) \
{ \
	int ret; \
	struct kprobe kp_##name = { \
		.symbol_name = #name, \
	}; \
	\
	ret = register_kprobe(&kp_##name); \
	if (ret < 0) { \
	pr_err("lookup " #name " fail!\n"); \
	return -1; \
	} \
	addr_##name = (__typeof__(addr_##name))kp_##name.addr; \
	unregister_kprobe(&kp_##name); \
	return 0; \
}

int hmbird_sysctl_init(void);
void hmbird_sysctl_deinit(void);


/* task sched prop define */

/*
 * Type: [prefer_cluster][prefer_cpu][dsq_idx]
 * Bits:  0-7             8-23        24-31
 * Type: [prefer_preempt][prefer_idle][set_uclamp][uclamp_keep_freq][uclamp_val][reserved]
 * Bits:  32              33           34          35                36-46       47-63
 */
static inline void hmbird_sched_prop_set_uclamp(struct task_struct *p, unsigned int val)
{
	struct scx_entity *scx;

	scx = get_oplus_ext_entity(p);
	if (unlikely(!scx))
		return;

	if (val) {
		scx->sched_prop = (scx->sched_prop & ~HMBIRD_SCHED_PROP_UCLAMP_VAL_MASK) |
			(((unsigned long)val << HMBIRD_SCHED_PROP_UCLAMP_VAL_SHIFT) & HMBIRD_SCHED_PROP_UCLAMP_VAL_MASK)
			| HMBIRD_SCHED_PROP_SET_UCLAMP;
	} else {
		scx->sched_prop &= ~HMBIRD_SCHED_PROP_SET_UCLAMP;
	}
}

static inline void hmbird_sched_prop_uclamp_keep_freq(struct task_struct *p, bool set)
{
	struct scx_entity *scx;

	scx = get_oplus_ext_entity(p);
	if (unlikely(!scx))
		return;

	if (set)
		scx->sched_prop |= HMBIRD_SCHED_PROP_UCLAMP_KEEP_FREQ;
	else
		scx->sched_prop &= ~HMBIRD_SCHED_PROP_UCLAMP_KEEP_FREQ;
}

static inline void hmbird_sched_prop_set_prefer_idle(struct task_struct *p, bool set)
{
	struct scx_entity *scx;

	scx = get_oplus_ext_entity(p);
	if (unlikely(!scx))
		return;

	if (set)
		scx->sched_prop |= HMBIRD_SCHED_PROP_PREFER_IDLE;
	else
		scx->sched_prop &= ~HMBIRD_SCHED_PROP_PREFER_IDLE;
}

static inline void hmbird_sched_prop_set_prefer_preempt(struct task_struct *p, bool set)
{
	struct scx_entity *scx;

	scx = get_oplus_ext_entity(p);
	if (unlikely(!scx))
		return;

	if (set)
		scx->sched_prop |= HMBIRD_SCHED_PROP_PREFER_PREEMPT;
	else
		scx->sched_prop &= ~HMBIRD_SCHED_PROP_PREFER_PREEMPT;
}

static inline void hmbird_sched_prop_set_prefer_cluster(struct task_struct *p,
					unsigned int clusters, bool set)
{
	struct scx_entity *scx;

	scx = get_oplus_ext_entity(p);
	if (unlikely(!scx))
		return;

	if (set)
		scx->sched_prop = (scx->sched_prop & ~HMBIRD_SCHED_PROP_PREFER_CLUSTER_MASK) |
				(clusters & HMBIRD_SCHED_PROP_PREFER_CLUSTER_MASK) |
				HMBIRD_SCHED_PROP_PREFER_CLUSTER_SET;
	else
		scx->sched_prop &= ~(HMBIRD_SCHED_PROP_PREFER_CLUSTER_MASK
					| HMBIRD_SCHED_PROP_PREFER_CLUSTER_SET);
}

static inline void hmbird_sched_prop_set_prefer_cpu(struct task_struct *p,
					unsigned int cpus, bool set)
{
	struct scx_entity *scx;

	scx = get_oplus_ext_entity(p);
	if (unlikely(!scx))
		return;

	if (set)
		scx->sched_prop = (scx->sched_prop & ~HMBIRD_SCHED_PROP_PREFER_CPU_MASK) |
				(((unsigned long)cpus << HMBIRD_SCHED_PROP_PREFER_CPU_SHIFT) & HMBIRD_SCHED_PROP_PREFER_CPU_MASK)
				| HMBIRD_SCHED_PROP_PREFER_CPU_SET;
	else
		scx->sched_prop &= ~(HMBIRD_SCHED_PROP_PREFER_CPU_MASK | HMBIRD_SCHED_PROP_PREFER_CPU_SET);
}

static inline void hmbird_sched_prop_set_sched_prop_directly(struct task_struct *p,
					u64 sched_prop, u64 sched_prop_mask)
{
	struct scx_entity *scx;
	u64 sched_prop_checked = 0;
	u64 sched_prop_mask_checked = 0;

	if (sched_prop_mask & HMBIRD_SCHED_PROP_PREFER_CLUSTER_SET) {
		sched_prop_checked |= (sched_prop & HMBIRD_SCHED_PROP_PREFER_CLUSTER_MASK) | HMBIRD_SCHED_PROP_PREFER_CLUSTER_SET;
		sched_prop_mask_checked |= HMBIRD_SCHED_PROP_PREFER_CLUSTER_MASK | HMBIRD_SCHED_PROP_PREFER_CLUSTER_SET;
	}
	if (sched_prop_mask & HMBIRD_SCHED_PROP_PREFER_CPU_SET) {
		sched_prop_checked |= (sched_prop & HMBIRD_SCHED_PROP_PREFER_CPU_MASK) | HMBIRD_SCHED_PROP_PREFER_CPU_SET;
		sched_prop_mask_checked |= HMBIRD_SCHED_PROP_PREFER_CPU_MASK | HMBIRD_SCHED_PROP_PREFER_CPU_SET;
	}
	if (sched_prop_mask & HMBIRD_SCHED_PROP_PREFER_PREEMPT) {
		sched_prop_checked |= sched_prop & HMBIRD_SCHED_PROP_PREFER_PREEMPT;
		sched_prop_mask_checked |= HMBIRD_SCHED_PROP_PREFER_PREEMPT;
	}
	if (sched_prop_mask & HMBIRD_SCHED_PROP_PREFER_IDLE) {
		sched_prop_checked |= sched_prop & HMBIRD_SCHED_PROP_PREFER_IDLE;
		sched_prop_mask_checked |= HMBIRD_SCHED_PROP_PREFER_IDLE;
	}
	if (sched_prop_mask & HMBIRD_SCHED_PROP_SET_UCLAMP) {
		sched_prop_checked |= (sched_prop & HMBIRD_SCHED_PROP_UCLAMP_VAL_MASK) | HMBIRD_SCHED_PROP_SET_UCLAMP;
		sched_prop_mask_checked |= HMBIRD_SCHED_PROP_UCLAMP_VAL_MASK | HMBIRD_SCHED_PROP_SET_UCLAMP;
	}
	if (sched_prop_mask & HMBIRD_SCHED_PROP_UCLAMP_KEEP_FREQ) {
		sched_prop_checked |= sched_prop & HMBIRD_SCHED_PROP_UCLAMP_KEEP_FREQ;
		sched_prop_mask_checked |= HMBIRD_SCHED_PROP_UCLAMP_KEEP_FREQ;
	}
	if (sched_prop_mask & HMBIRD_SCHED_PROP_DSQ_IDX_MASK) {
		sched_prop_checked |= sched_prop & HMBIRD_SCHED_PROP_DSQ_IDX_MASK;
		sched_prop_mask_checked |= HMBIRD_SCHED_PROP_DSQ_IDX_MASK;
	}
	scx = get_oplus_ext_entity(p);
	if (unlikely(!scx))
		return;

	scx->sched_prop = (scx->sched_prop & ~sched_prop_mask_checked) | sched_prop_checked;
}

static inline void hmbird_sched_prop_set_dsq_idx(struct task_struct *p,
					int dsq_idx, bool set)
{
	struct scx_entity *scx;

	scx = get_oplus_ext_entity(p);
	if (unlikely(!scx))
		return;

	if (set)
		scx->sched_prop = (scx->sched_prop & ~HMBIRD_SCHED_PROP_DSQ_IDX_MASK) |
				(((unsigned long)dsq_idx << HMBIRD_SCHED_PROP_DSQ_SHIFT) & HMBIRD_SCHED_PROP_DSQ_IDX_MASK);
	else
		scx->sched_prop &= ~HMBIRD_SCHED_PROP_DSQ_IDX_MASK;
}

extern void cfg_cpus_get(cpumask_t *cpus_exclusive, cpumask_t *cpus_reserved);
extern int cfg_frame_per_sec_get(void);
extern unsigned long cfg_coefficient_get(int cluster_id);
extern unsigned long cfg_perf_high_ratio_get(int cluster_id);
extern unsigned long cfg_freq_policy_get(int cluster_id);
#define DIV64_U64_ROUNDUP(X, Y) div64_u64((X) + (Y - 1), Y)
void hmbird_qos_request_min(const struct cpumask *cpus, unsigned int min_freq);
int hmbird_II_init(void);
void hmbird_II_exit(void);

#endif /* _HMBIRD_II_H_ */

