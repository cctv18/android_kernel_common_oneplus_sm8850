#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/bpf.h>
#include <linux/bpf_verifier.h>
#include <linux/cpumask.h>
#include <linux/mmu_context.h>
#include <linux/jump_label.h>
#include "../../kernel/sched/sched.h"
#include <trace/hooks/sched.h>
#include "hmbird_II.h"
#include "hmbird_II_export.h"
#include "hmbird_II_freqgov.h"
#include "hmbird_II_shadow_tick.h"
#include "../hmbird_minidump.h"
#include "hmbird_common.h"
#include "hmbird_II_critical_task_monitor.h"
#include <linux/sched/ext.h>
#include <linux/random.h>
#include <trace/hooks/sched.h>
#include <trace/events/task.h>
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("OPLUS KERNEL III TEAM");
MODULE_VERSION("1.0");
#define wlog(fmt, ...)	pr_err(fmt, ##__VA_ARGS__)

enum scx_ops_enable_state {
	SCX_OPS_ENABLING,
	SCX_OPS_ENABLED,
	SCX_OPS_DISABLING,
	SCX_OPS_DISABLED,
	SCX_OPS_DISABLING_SWITCH,
};
extern enum hmbird_switch_reason_type sw_reason;
extern struct md_info_t *md_info;

#define MAX_STATE 10
const int scx2hmbird_state[MAX_STATE] = {1, 4, 2, 0, 3, 5, 6, 7, 8, 9};

static atomic_t hb_ops_enable_state_var = ATOMIC_INIT(SCX_OPS_DISABLED);
static atomic_t __hb_ops_enabled = ATOMIC_INIT(0);
unsigned int hmbird_enable = 0;

static inline bool hb_task_should_scx(int policy)
{
	if (!atomic_read(&__hb_ops_enabled))
		return false;

	if (unlikely(atomic_read(&hb_ops_enable_state_var) == SCX_OPS_DISABLING)) {
		if (current == sched_ext_helper) {
			atomic_xchg(&hb_ops_enable_state_var, SCX_OPS_DISABLING_SWITCH);
		}
	}

	if (!sched_ext_helper && atomic_read(&hb_ops_enable_state_var) == SCX_OPS_DISABLING)
		return false;

	if (unlikely(atomic_read(&hb_ops_enable_state_var) == SCX_OPS_DISABLING_SWITCH))
		return false;

	return true;
}

void hmbird_state_systrace_c(void)
{
	int scx_ops_state, hmbird_state;

	scx_ops_state = atomic_read(&hb_ops_enable_state_var);
	if (scx_ops_state < 0 && scx_ops_state >= MAX_STATE) {
		return;
	}
	hmbird_state = scx2hmbird_state[scx_ops_state];

	hmbird_II_output_systrace("C|9999|hmbird_state|%d\n", hmbird_state);
}

/*
 * workaround for a bug while disable sched_ext
 * This case is fixed in kernel 6.17 :
 * Fix a realtime tasks starvation case where failure to enqueue a timer
 * whose expiration time is already in the past would cause repeated
 * attempts to re-enqueue a deadline server task which leads to starving
 * the former, realtime one
 * https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/commit/?id=fe3ad7a58b581859a1a7c237b670f8bcbf5b253c
 *
 * Once bug is fixed, remove the workaround function
 */
void replenish_fair_dl_server(void)
{
		int cpu;
		struct rq *rq;
		struct rq_flags rf;
		struct sched_dl_entity *dl_se;

		for_each_possible_cpu(cpu) {
			rq = cpu_rq(cpu);
			rq_lock_irqsave(rq, &rf);
			dl_se = &rq->fair_server;
			dl_se->dl_throttled = 1;
			dl_se->runtime = 0;
			rq_unlock_irqrestore(rq, &rf);
		}
}

static void android_vh_scx_ops_enable_state(void *unused, int state)
{
	int state_prev;
	struct cpumask boost = { .bits[0] = 0xff };
	state_prev = atomic_xchg(&hb_ops_enable_state_var, state);
	if (state == SCX_OPS_DISABLED && state_prev != SCX_OPS_DISABLING_SWITCH) {
		pr_warn("sched_ext: ops error detected without ops (SCX_OPS_DISABLING_SWITCH)\n");
		sw_update(0, HMBIRD_DISABLED_WITHOUT_ING, HMBIRD_SWITCH_NORMAL);
	}
	if (state == SCX_OPS_ENABLING || state == SCX_OPS_DISABLING) {
		hmbird_qos_request_min(&boost, FREQ_QOS_MAX_DEFAULT_VALUE);
	} else {
		hmbird_qos_request_min(&boost, FREQ_QOS_MIN_DEFAULT_VALUE);
	}

	if (state == SCX_OPS_DISABLING) {
		replenish_fair_dl_server();
	}

	pr_info("hmbird_II: scx_ops_enable_state = %d\n", atomic_read(&hb_ops_enable_state_var));
	if (unlikely(hmbird_debug & HMBIRD_DEBUG_SYSTRACE))
		hmbird_state_systrace_c();

	if (state >= 0 && state < MAX_STATE) {
		sw_update(1, scx2hmbird_state[state], HMBIRD_SWITCH_NORMAL);
	}
}

static void android_vh_scx_enabled(void *unused, int enabled)
{
	atomic_xchg(&__hb_ops_enabled, enabled);
	pr_info("hmbird_II: scx %s\n", atomic_read(&__hb_ops_enabled) ? "enabled" : "disabled");
	sw_update(1, enabled ? HMBIRD_ENABLED : HMBIRD_DISABLED, HMBIRD_SWITCH_NORMAL);
}

static void android_vh_task_should_scx(void *unused, int *should_scx, int policy, int prio)
{
	if (hmbird_enable != HMBIRD_II_SCENE)
		return;
	if (hb_task_should_scx(policy) && prio > 10)
		*should_scx = 1;
}

static void android_vh_scx_ops_consider_migration(void *unused, bool *consider_migration)
{
	if (hmbird_enable != HMBIRD_II_SCENE)
		return;
	if (atomic_read(&__hb_ops_enabled))
		*consider_migration = true;
}

static void android_vh_fix_prev_keep_slice(void *unused, struct task_struct *p)
{
	if (hmbird_enable != HMBIRD_II_SCENE)
		return;
	if (atomic_read(&__hb_ops_enabled))
		p->scx.slice = 1000000;
}

void android_vh_dup_task_struct_handler(void *unused,
		struct task_struct *tsk, struct task_struct *orig)
{
	struct scx_entity *scx,*orig_scx;

	if (!tsk || !orig)
		return;

	scx = get_oplus_ext_entity(tsk);
	orig_scx = get_oplus_ext_entity(orig);
	if (!scx || !orig_scx)
		return;

	cpumask_copy(&scx->cpus_mask_back, &orig_scx->cpus_mask_back);
	scx->need_back = orig_scx->need_back;
	scx->sched_prop = 0;
}

static void hmbird_task_change_cpumask(struct task_struct *p)
{
	struct scx_entity *scx;

	if (hmbird_enable != HMBIRD_II_SCENE)
		return;

	scx = get_oplus_ext_entity(p);
	if (!scx)
		return;

	if (0) {
		if (!p->user_cpus_ptr) {
			scx->need_back = true;
			cpumask_copy(&scx->cpus_mask_back, &p->cpus_mask);
			cpumask_copy(&p->cpus_mask, task_cpu_possible_mask(p));
			p->nr_cpus_allowed = cpumask_weight(&p->cpus_mask);
		}
	}
	scx->need_back = true;
	cpumask_copy(&scx->cpus_mask_back, &p->cpus_mask);
	cpumask_copy(&p->cpus_mask, task_cpu_possible_mask(p));
	p->nr_cpus_allowed = cpumask_weight(&p->cpus_mask);
}

static void hmbird_task_restore_cpumask(struct task_struct *p)
{
	struct scx_entity *scx;

	if (hmbird_enable != HMBIRD_II_SCENE)
		return;

	scx = get_oplus_ext_entity(p);
	if (!scx)
		return;

	if (0) {
		if (!p->user_cpus_ptr || scx->need_back) {
			scx->need_back = false;
			cpumask_copy(&p->cpus_mask, &scx->cpus_mask_back);
			p->nr_cpus_allowed = cpumask_weight(&p->cpus_mask);
		}
	}
	if (scx->need_back) {
		scx->need_back = false;
		cpumask_copy(&p->cpus_mask, &scx->cpus_mask_back);
		p->nr_cpus_allowed = cpumask_weight(&p->cpus_mask);
	}
}

static void trace_task_change_cpumask(void *unused, struct task_struct *p, int enable)
{
	if (hmbird_enable != HMBIRD_II_SCENE)
		return;
	if (p->nr_cpus_allowed == 1)
		return;
	if (enable) {
		hmbird_task_change_cpumask(p);
	} else {
		hmbird_task_restore_cpumask(p);
	}
}

static void trace_set_cpus_allowed_common(void *unused, struct task_struct *p, struct affinity_context *ctx, int *done)
{
	struct scx_entity *scx;

	if (hmbird_enable != HMBIRD_II_SCENE)
		return;

	*done = 1;
	if (ctx->flags & (SCA_MIGRATE_ENABLE | SCA_MIGRATE_DISABLE)) {
		p->cpus_ptr = ctx->new_mask;
		return;
	}

	scx = get_oplus_ext_entity(p);
	if (!scx)
		return;
	{
	if (0) {

		if (ctx->flags & SCA_USER || cpumask_weight(ctx->new_mask) == 1) {
			cpumask_copy(&p->cpus_mask, ctx->new_mask);
			scx->need_back = false;
			p->nr_cpus_allowed = cpumask_weight(ctx->new_mask);
			swap(p->user_cpus_ptr, ctx->user_mask);
		} else {
			cpumask_copy(&scx->cpus_mask_back, ctx->new_mask);
			scx->need_back = true;
			if (p->user_cpus_ptr) {
				cpumask_and(&p->cpus_mask, p->user_cpus_ptr, task_cpu_possible_mask(p));
			} else {
				cpumask_copy(&p->cpus_mask, task_cpu_possible_mask(p));
			}
			p->nr_cpus_allowed = cpumask_weight(&p->cpus_mask);
		}
	}
	}
	if (cpumask_weight(ctx->new_mask) == 1) {
		scx->need_back = false;
		cpumask_copy(&p->cpus_mask, ctx->new_mask);
		p->nr_cpus_allowed = cpumask_weight(ctx->new_mask);
		if (ctx->flags & SCA_USER)
			swap(p->user_cpus_ptr, ctx->user_mask);
	} else {
		cpumask_copy(&scx->cpus_mask_back, ctx->new_mask);
		scx->need_back = true;
		cpumask_copy(&p->cpus_mask, task_cpu_possible_mask(p));
		p->nr_cpus_allowed = cpumask_weight(&p->cpus_mask);
		if (ctx->flags & SCA_USER)
			swap(p->user_cpus_ptr, ctx->user_mask);
	}
}

void get_scx_state(int *ops_enabled, int *ops_enable_state_var)
{
	*ops_enabled = atomic_read(&__hb_ops_enabled);
	*ops_enable_state_var = atomic_read(&hb_ops_enable_state_var);
}

noinline int tracing_mark_write(const char *buf)
{
	trace_printk(buf);
	return 0;
}

int nr_cluster;
struct hmbird_sched_cluster hmbird_cluster[MAX_NR_CLUSTER];


static DEFINE_MUTEX(qos_lock);
static DEFINE_PER_CPU(struct freq_qos_request, hmbird_II_qos_min);

struct hmbird_qos_req {
	struct delayed_work qos_work;
	struct cpumask req_mask;
	unsigned int min_freq;
	unsigned int max_freq; // may be unused now
};

static struct hmbird_qos_req request_req, reset_req;

#define FREQ_QOS_REQ_MAX_MS				(5 * MSEC_PER_SEC)

static void hmbird_qos_update_handler(struct work_struct *work)
{
	int cpu;
	struct cpumask req_mask;
	struct freq_qos_request *req;
	struct cpufreq_policy *policy;
	struct hmbird_qos_req *hreq = container_of((struct delayed_work *)work, struct hmbird_qos_req, qos_work);
	if (hreq != &reset_req)
		cancel_delayed_work_sync(&reset_req.qos_work);
	mutex_lock(&qos_lock);
	cpus_read_lock();
	cpumask_and(&req_mask, &hreq->req_mask, cpu_present_mask);
	for_each_cpu(cpu, &req_mask) {
		policy = cpufreq_cpu_get_raw(cpu);
		if (!policy) {
			continue;
		}
		req = &per_cpu(hmbird_II_qos_min, policy->cpu);
		cpumask_andnot(&req_mask, &req_mask, policy->related_cpus);
		freq_qos_update_request(req, hreq->min_freq);
	}
	cpus_read_unlock();
	mutex_unlock(&qos_lock);
	if (hreq != &reset_req && hreq->min_freq != FREQ_QOS_MIN_DEFAULT_VALUE)
		schedule_delayed_work(&reset_req.qos_work, msecs_to_jiffies(FREQ_QOS_REQ_MAX_MS));
}

void hmbird_qos_request_min(const struct cpumask *cpus, unsigned int min_freq)
{
	cancel_delayed_work(&request_req.qos_work);
	cpumask_copy(&request_req.req_mask, cpus);
	request_req.min_freq = min_freq;
	queue_delayed_work_on(0, system_highpri_wq, &request_req.qos_work, 0);
}

static inline void hmbird_freq_qos_request_exit(void)
{
	struct freq_qos_request *req;
	int cpu;
	for_each_present_cpu(cpu) {
		req = &per_cpu(hmbird_II_qos_min, cpu);
		if (req && freq_qos_request_active(req))
			freq_qos_remove_request(req);
	}
}

static int hmbird_freq_qos_request_init(void)
{
	unsigned int cpu;
	int ret;

	struct cpufreq_policy *policy;
	struct freq_qos_request *req;
	reset_req.req_mask.bits[0] = 0xff;
	INIT_DELAYED_WORK(&request_req.qos_work, hmbird_qos_update_handler);
	INIT_DELAYED_WORK(&reset_req.qos_work, hmbird_qos_update_handler);

	for_each_cpu(cpu, cpu_possible_mask) {
		policy = cpufreq_cpu_get(cpu);
		if (!policy) {
			HMBIRD_ERR("%s: Failed to get cpufreq policy for cpu%d\n",
				__func__, cpu);
			ret = -EINVAL;
			goto cleanup;
		}

		req = &per_cpu(hmbird_II_qos_min, cpu);
		ret = freq_qos_add_request(&policy->constraints, req,
			FREQ_QOS_MIN, FREQ_QOS_MIN_DEFAULT_VALUE);
		if (ret < 0) {
			HMBIRD_ERR("%s: Failed to add min freq constraint (%d)\n",
				__func__, ret);
			cpufreq_cpu_put(policy);
			goto cleanup;
		}

		cpufreq_cpu_put(policy);
	}
	return 0;

cleanup:
	hmbird_freq_qos_request_exit();
	return ret;
}

static void hmbird_build_clusters(void)
{
	int cpu;
	struct hmbird_sched_cluster *cluster;
	int cluster_id;
	memset(hmbird_cluster, 0, sizeof(struct hmbird_sched_cluster) * MAX_NR_CLUSTER);

	for_each_cpu(cpu, cpu_possible_mask) {
		cluster_id = topology_cluster_id(cpu);
		if (cluster_id < 0 || cluster_id >= MAX_NR_CLUSTER) {
			HMBIRD_ERR("build clusters bug!\n");
			break;
		}
		cpumask_set_cpu(cpu, &hmbird_cluster[cluster_id].cpus);
	}

	/* check nr_cluster */
	for (cluster_id = 0; cluster_id < MAX_NR_CLUSTER - 1; cluster_id++) {
		cluster = &hmbird_cluster[cluster_id];
		if (cpumask_empty(&cluster->cpus))
			break;
		cluster->id = cluster_id;
		nr_cluster++;
	}

	hmbird_cluster[nr_cluster].id = MAX_NR_CLUSTER;

	for_each_sched_cluster(cluster) {
		for_each_cpu(cpu, &cluster->cpus) {
			pr_err("hmbird_II::for_each_cluster=%d, cpu=%d\n", cluster->id, cpu);
		}
	}
}

extern int critical_task_monitor_init(void);
extern int critical_task_monitor_deinit(void);

int hmbird_II_init(void)
{
	hmbird_build_clusters();
	hmbird_sysctl_init();
	hmbird_freqgov_init();
	hmbird_shadow_tick_init();
	critical_task_monitor_init();
	hmbird_freq_qos_request_init();
	register_trace_android_vh_task_should_scx(android_vh_task_should_scx, NULL);
	register_trace_android_vh_scx_ops_consider_migration(android_vh_scx_ops_consider_migration, NULL);
	register_trace_android_vh_scx_fix_prev_slice(android_vh_fix_prev_keep_slice, NULL);
	register_trace_android_vh_scx_enabled(android_vh_scx_enabled, NULL);
	register_trace_android_vh_scx_ops_enable_state(android_vh_scx_ops_enable_state, NULL);
	register_trace_android_vh_dup_task_struct(android_vh_dup_task_struct_handler, NULL);
	register_trace_android_vh_scx_task_switch_finish(trace_task_change_cpumask, NULL);
	register_trace_android_vh_scx_set_cpus_allowed(trace_set_cpus_allowed_common, NULL);
	return 0;
}

void hmbird_II_exit(void)
{
	hmbird_sysctl_deinit();
	critical_task_monitor_deinit();
	ko_exceps_update(KO_DEINITED, jiffies);
}

