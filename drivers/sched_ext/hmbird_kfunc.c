#include <linux/bpf.h>
#include <linux/btf.h>
#include <linux/btf_ids.h>
#include <linux/cpumask.h>
#include <linux/cpufreq.h>
#include <uapi/linux/bpf.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/mmu_context.h>
#include "../kernel/sched/sched.h"
#include <linux/kprobes.h>
#include "hmbird_CameraScene/hmbird_CameraScene.h"
#include "hmbird_II/hmbird_II_export.h"
#include "hmbird_II/hmbird_II.h"
#include "hmbird_kfunc.h"
#include "hmbird_common.h"
#include "hmbird_dfx.h"
#include "hmbird_minidump.h"

extern bool test_task_ux(struct task_struct *task);
extern unsigned int hmbird_bpf_log_level;
#ifdef CONFIG_OPLUS_SYSTEM_KERNEL_MTK
extern int (*addr_get_vip_task_prio)(struct task_struct *p);
#endif
struct scx_dispatch_q *(*addr_find_user_dsq)(u64 dsq_id);
LOOKUP_KERNEL_SYMBOL(find_user_dsq);

static int hmbird_II_lookup_symbols(void)
{
	return lookup_find_user_dsq();
}

__bpf_kfunc_start_defs();

__bpf_kfunc unsigned int hb_bpf_hmbird_bpf_log_level(void)
{
	return hmbird_bpf_log_level;
}

__bpf_kfunc bool hb_bpf_is_ux(struct task_struct *p)
{
	if (unlikely(!global_sched_assist_enabled))
		return false;

	if (!test_task_is_fair(p))
		return false;

	return oplus_get_ux_state(p) & SCHED_ASSIST_UX_MASK;
}

__bpf_kfunc int hb_bpf_get_ux_state(struct task_struct *p)
{
	return oplus_get_ux_state(p);
}

__bpf_kfunc bool hb_bpf_is_vip_mvp(struct task_struct *p)
{
#ifdef CONFIG_OPLUS_SYSTEM_KERNEL_MTK
	if (addr_get_vip_task_prio && addr_get_vip_task_prio(p) != -1)
		return true;
#endif
	return false;
}

__bpf_kfunc int hb_bpf_tracing_mark_write(const char *buf)
{
	tracing_mark_write(buf);
	return 0;
}

__bpf_kfunc unsigned int hb_bpf_cfg_hmbird_debug(void)
{
	return hmbird_debug;
}

__bpf_kfunc __nocfi struct scx_dispatch_q *hb_bpf_find_user_dsq(u64 dsq_id)
{
	return addr_find_user_dsq(dsq_id);
}

__bpf_kfunc struct task_struct *hb_bpf_dsq_next_task(struct scx_dispatch_q *dsq, struct task_struct *cur)
{
	struct scx_dsq_list_node *dsq_lnode;
	struct list_head *list_node;

	lockdep_assert_held(&dsq->lock);

	if (cur) {
		if (unlikely(cur->scx.dsq != dsq)) {
			HMBIRD_ERR("task[%s][%d] doesn't belong to dsq=0x%llx\n", cur->comm, cur->pid, dsq->id);
			return NULL;
		}
		list_node = &cur->scx.dsq_list.node;
	} else {
		list_node = &dsq->list;
	}

	list_node = list_node->next;

	if (list_node == &dsq->list)
		return NULL;

	dsq_lnode = container_of(list_node, struct scx_dsq_list_node, node);

	return container_of(dsq_lnode, struct task_struct, scx.dsq_list);
}

__bpf_kfunc struct task_struct *hb_bpf_dsq_first_task(struct scx_dispatch_q *dsq)
{
	return hb_bpf_dsq_next_task(dsq, NULL);
}


__bpf_kfunc int hb_bpf_topology_cluster_id(unsigned int cpu)
{
	if (likely(cpu >= 0 && cpu < MAX_NR_CPUS))
		return topology_cluster_id(cpu);
	return -1;
}

__bpf_kfunc void hb_bpf_set_prefer_cpu(struct task_struct *p, unsigned long cpus)
{
	hmbird_sched_prop_set_prefer_cpu(p, cpus, 1);
}

__bpf_kfunc void hb_bpf_set_prefer_preempt(struct task_struct *p)
{
	hmbird_sched_prop_set_prefer_preempt(p, 1);
}

__bpf_kfunc void hb_bpf_set_prefer_idle(struct task_struct *p)
{
	hmbird_sched_prop_set_prefer_idle(p, 1);
}

__bpf_kfunc u64 hb_bpf_task_sched_prop(struct task_struct *p)
{
	struct scx_entity *scx = get_oplus_ext_entity(p);
	if (unlikely(!scx))
		return 0;
	return scx->sched_prop;
}

__bpf_kfunc int hb_bpf_sched_setscheduler(struct task_struct *p, int policy, int sched_priority)
{
	struct sched_param param = { .sched_priority = sched_priority};
	return sched_setscheduler_nocheck(p, policy, &param);
}

__bpf_kfunc int hb_bpf_set_cpus_allowed_ptr(struct task_struct *p, const struct cpumask *new_mask)
{
	return set_cpus_allowed_ptr(p, new_mask);
}

__bpf_kfunc void hb_bpf_register_sched_ext_helper(struct task_struct *p)
{
	sched_ext_helper = p;
}

__bpf_kfunc void hb_bpf_cfg_cpus_get(cpumask_t *cpus_exclusive, cpumask_t *cpus_reserved)
{
	cfg_cpus_get(cpus_exclusive, cpus_reserved);
}

__bpf_kfunc int hb_bpf_cfg_frame_per_sec_get(void)
{
	return cfg_frame_per_sec_get();
}

__bpf_kfunc unsigned long hb_bpf_cfg_coefficient_get(int cluster_id)
{
	return cfg_coefficient_get(cluster_id);
}

__bpf_kfunc unsigned long hb_bpf_cfg_perf_high_ratio_get(int cluster_id)
{
	return cfg_perf_high_ratio_get(cluster_id);
}

__bpf_kfunc unsigned long hb_bpf_cfg_freq_policy_get(int cluster_id)
{
	return cfg_freq_policy_get(cluster_id);
}

/* export cpumask ops */
__bpf_kfunc void hb_bpf_raw_spin_lock_irqsave(raw_spinlock_t *lock, unsigned long *flags)
{
	unsigned long irqflags;
	local_irq_save(irqflags);
	raw_spin_lock(lock);
	*flags = irqflags;
}

__bpf_kfunc void hb_bpf_raw_spin_unlock_irqrestore(raw_spinlock_t *lock, unsigned long flags)
{
	raw_spin_unlock(lock);
	local_irq_restore(flags);
}

__bpf_kfunc void hb_bpf_resched_curr(struct rq *rq)
{
	resched_curr(rq);
}

__bpf_kfunc void hb_bpf_hmbird_II_init_deinit(bool init)
{
	if (init) {
		cpu_rq(0)->scx.flags |= SCX_RQ_BYPASS_HOOK;
		hmbird_enable = HMBIRD_II_SCENE;
	} else {
		hmbird_enable = 0;
		cpu_rq(0)->scx.flags &= ~SCX_RQ_BYPASS_HOOK;
	}
}

__bpf_kfunc void hb_bpf_multimedia_init_deinit(bool init)
{
	if (init) {
		cpu_rq(0)->scx.flags |= SCX_RQ_BYPASS_HOOK;
		hmbird_enable = MULTIMEDIA_SCENE;
	} else {
		hmbird_enable = 0;
		cpu_rq(0)->scx.flags &= ~SCX_RQ_BYPASS_HOOK;
	}
}

__bpf_kfunc unsigned long hb_bpf_check_pipeline_delayed(void)
{
	unsigned long max_delayed;

	if (!boost_enable)
		return 0;

	raw_spin_lock(&pipeline_lock);
	max_delayed = check_pipeline_delayed_locked();
	raw_spin_unlock(&pipeline_lock);
	return max_delayed;
}

__bpf_kfunc void hb_bpf_notify_gpa_exit_hmbird(void)
{
	notify_gpa_exit_hmbird();
}

__bpf_kfunc void hb_bpf_notify_manager_exit(void)
{
	notify_manager_exit();
}

__bpf_kfunc int hb_bpf_exceps_update(int id)
{
	bpf_exceps_update(id, jiffies);
	return 0;
}

__bpf_kfunc int hb_bpf_snap_misc_update(int nr_dsq, int nr_aval_clus)
{
	bpf_snapshot_misc_update(nr_dsq, nr_aval_clus);
	return 0;
}

__bpf_kfunc int hb_bpf_snap_dsq_update(u64 easy_dsq_id, u64 runnable_at, u64 dsq_timeout)
{
	bpf_snapshot_dsq_update(easy_dsq_id, runnable_at, dsq_timeout);
	return 0;
}


__bpf_kfunc_end_defs();


BTF_KFUNCS_START(hmbird_kfunc_ids_any)
/* hmbird common kfunc interface */
BTF_ID_FLAGS(func, hb_bpf_hmbird_bpf_log_level);
BTF_ID_FLAGS(func, hb_bpf_is_ux);
BTF_ID_FLAGS(func, hb_bpf_get_ux_state);
BTF_ID_FLAGS(func, hb_bpf_is_vip_mvp);
BTF_ID_FLAGS(func, hb_bpf_tracing_mark_write);
BTF_ID_FLAGS(func, hb_bpf_cfg_hmbird_debug);
BTF_ID_FLAGS(func, hb_bpf_exceps_update);
BTF_ID_FLAGS(func, hb_bpf_snap_misc_update);
BTF_ID_FLAGS(func, hb_bpf_snap_dsq_update);

/* hmbird II bpf kfunc interface */
BTF_ID_FLAGS(func, hb_bpf_find_user_dsq);
BTF_ID_FLAGS(func, hb_bpf_dsq_next_task);
BTF_ID_FLAGS(func, hb_bpf_dsq_first_task);
BTF_ID_FLAGS(func, hb_bpf_topology_cluster_id);
BTF_ID_FLAGS(func, hb_bpf_task_sched_prop);
BTF_ID_FLAGS(func, hb_bpf_sched_setscheduler);
BTF_ID_FLAGS(func, hb_bpf_set_cpus_allowed_ptr);
BTF_ID_FLAGS(func, hb_bpf_register_sched_ext_helper);
BTF_ID_FLAGS(func, hb_bpf_cfg_cpus_get);
BTF_ID_FLAGS(func, hb_bpf_cfg_frame_per_sec_get);
BTF_ID_FLAGS(func, hb_bpf_cfg_coefficient_get);
BTF_ID_FLAGS(func, hb_bpf_cfg_perf_high_ratio_get);
BTF_ID_FLAGS(func, hb_bpf_cfg_freq_policy_get);
BTF_ID_FLAGS(func, hb_bpf_set_prefer_cpu);
BTF_ID_FLAGS(func, hb_bpf_set_prefer_preempt);
BTF_ID_FLAGS(func, hb_bpf_set_prefer_idle);
BTF_ID_FLAGS(func, hb_bpf_raw_spin_lock_irqsave);
BTF_ID_FLAGS(func, hb_bpf_raw_spin_unlock_irqrestore);
BTF_ID_FLAGS(func, hb_bpf_resched_curr);
BTF_ID_FLAGS(func, hb_bpf_hmbird_II_init_deinit);
BTF_ID_FLAGS(func, hb_bpf_multimedia_init_deinit);

/* CameraScene bpf kfunc interface */
BTF_ID_FLAGS(func, hb_bpf_check_pipeline_delayed);

/* DFX bpf kfunc interface */
BTF_ID_FLAGS(func, hb_bpf_notify_gpa_exit_hmbird);
BTF_ID_FLAGS(func, hb_bpf_notify_manager_exit);

BTF_KFUNCS_END(hmbird_kfunc_ids_any)

static const struct btf_kfunc_id_set hmbird_kfunc_set_any = {
	.owner = THIS_MODULE,
	.set = &hmbird_kfunc_ids_any,
};

void pre_hmbird_kfunc_register(void)
{
	hmbird_II_lookup_symbols();
}

void hmbird_kfunc_register(void)
{
	int ret;
	ret = register_btf_kfunc_id_set(BPF_PROG_TYPE_STRUCT_OPS, &hmbird_kfunc_set_any)
		|| register_btf_kfunc_id_set(BPF_PROG_TYPE_TRACING, &hmbird_kfunc_set_any);

	if (ret)
	pr_err("hmbird_kfunc_register fail\n");
}
