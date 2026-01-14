#include <linux/tick.h>
#include <kernel/time/tick-sched.h>
#include <trace/hooks/sched.h>
#include "../../kernel/sched/sched.h"
#include "hmbird_II.h"

#define HIGHRES_WATCH_CPU       0
unsigned int highres_tick_ctrl;
unsigned int highres_tick_ctrl_dbg;

#define shadow_tick_printk(fmt, args...)	\
do {							\
	int cpu = smp_processor_id();			\
	if (highres_tick_ctrl_dbg && cpu == HIGHRES_WATCH_CPU)	\
		trace_printk("hmbird shadow tick :"fmt, args);	\
} while (0)


#define REGISTER_TRACE_VH(vender_hook, handler) \
{ \
	ret = register_trace_##vender_hook(handler, NULL); \
	if (ret) { \
		pr_err("failed to register_trace_"#vender_hook", ret=%d\n", ret); \
	} \
}

#define NUM_SHADOW_TICK_TIMER (3)
DEFINE_PER_CPU(struct hrtimer[NUM_SHADOW_TICK_TIMER], stt);
#define shadow_tick_timer(cpu, id) (&per_cpu(stt[id], (cpu)))

#define STOP_IDLE_TRIGGER     (1)
#define PERIODIC_TICK_TRIGGER (2)
static DEFINE_PER_CPU(u8, trigger_event);

static void sched_switch_handler(void *data, bool preempt, struct task_struct *prev,
		struct task_struct *next, unsigned int prev_state)
{
	int i, cpu = smp_processor_id();

	if (highres_tick_ctrl && (cpu_rq(cpu)->idle == prev)) {
		this_cpu_write(trigger_event, STOP_IDLE_TRIGGER);
		for (i = 0; i < NUM_SHADOW_TICK_TIMER; i++) {
			if (!hrtimer_active(shadow_tick_timer(cpu, i)))
				hrtimer_start(shadow_tick_timer(cpu, i),
					ns_to_ktime(1000000ULL * (i + 1)), HRTIMER_MODE_REL_PINNED);
		}
		if (highres_tick_ctrl_dbg && cpu == HIGHRES_WATCH_CPU)
			trace_printk("hmbird_sched : enter tick triggered by stop_idle events\n");
	}
}

enum hrtimer_restart scheduler_tick_no_balance(struct hrtimer *timer)
{
	int cpu = smp_processor_id();
	struct rq *rq = cpu_rq(cpu);
	struct task_struct *curr = rq->curr;
	struct rq_flags rf;

	rq_lock(rq, &rf);
	update_rq_clock(rq);
	curr->sched_class->task_tick(rq, curr, 0);
	shadow_tick_printk("enter 1ms tick on cpu%d \n", HIGHRES_WATCH_CPU);
	rq_unlock(rq, &rf);
	return HRTIMER_NORESTART;
}

void shadow_tick_timer_init(void)
{
	int i, cpu;

	for_each_possible_cpu(cpu) {
		for (i = 0; i < NUM_SHADOW_TICK_TIMER; i++) {
			hrtimer_init(shadow_tick_timer(cpu, i),
					CLOCK_MONOTONIC, HRTIMER_MODE_REL);
			shadow_tick_timer(cpu, i)->function =
					&scheduler_tick_no_balance;
		}
	}
}

void start_shadow_tick_timer(void)
{
	int i, cpu = smp_processor_id();

	if (highres_tick_ctrl) {
		if (this_cpu_read(trigger_event) == STOP_IDLE_TRIGGER) {
			for (i = 0; i < NUM_SHADOW_TICK_TIMER; i++)
				hrtimer_cancel(shadow_tick_timer(cpu, i));
		}

		this_cpu_write(trigger_event, PERIODIC_TICK_TRIGGER);

		for (i = 0; i < NUM_SHADOW_TICK_TIMER; i++) {
			if (!hrtimer_active(shadow_tick_timer(cpu, i)))
				hrtimer_start(shadow_tick_timer(cpu, i),
						ns_to_ktime(1000000ULL * (i + 1)),
						HRTIMER_MODE_REL_PINNED);
			shadow_tick_printk("restart 1ms tick on cpu%d \n",
							HIGHRES_WATCH_CPU);
		}
	}
}

static void stop_shadow_tick_timer(void)
{
	int i, cpu = smp_processor_id();

	this_cpu_write(trigger_event, 0);
	for (i = 0; i < NUM_SHADOW_TICK_TIMER; i++)
		hrtimer_cancel(shadow_tick_timer(cpu, i));
	shadow_tick_printk("stop 1ms tick on cpu%d \n", HIGHRES_WATCH_CPU);
}

void android_vh_tick_nohz_idle_stop_tick_handler(void *unused, void *data)
{
	stop_shadow_tick_timer();
}

#define HM_SYSTRACE_INTERVAL_JIFF	msecs_to_jiffies(3000)
extern void hmbird_state_systrace_c(void);
extern void gov_switch_state_systrace_c(void);
static volatile unsigned long next_systrace_jiff;
static volatile u64 in_output;

static void scheduler_tick_handler(void *unused, struct rq *rq)
{
	start_shadow_tick_timer();

	if (unlikely(hmbird_debug & HMBIRD_DEBUG_SYSTRACE)) {
		if (__sync_val_compare_and_swap(&in_output, 0, 1)) {
			goto skip_systrace;
		}
		if (time_before(jiffies, next_systrace_jiff)) {
			in_output = 0;
			goto skip_systrace;
		}
		hmbird_state_systrace_c();
		gov_switch_state_systrace_c();
		next_systrace_jiff = jiffies + HM_SYSTRACE_INTERVAL_JIFF;
		in_output = 0;
	}
skip_systrace:
	return;
}

int hmbird_shadow_tick_init(void)
{
	int ret = 0;
	shadow_tick_timer_init();
	REGISTER_TRACE_VH(android_vh_tick_nohz_idle_stop_tick,
				android_vh_tick_nohz_idle_stop_tick_handler);

	REGISTER_TRACE_VH(android_vh_scheduler_tick,
				scheduler_tick_handler);

	REGISTER_TRACE_VH(sched_switch, sched_switch_handler);
	return ret;
}

