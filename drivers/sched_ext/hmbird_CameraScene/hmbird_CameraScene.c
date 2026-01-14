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
#include <linux/sched/ext.h>
#include <linux/random.h>
#include <trace/hooks/sched.h>
#include <trace/events/task.h>
#include "hmbird_CameraScene.h"

int hmbird_CameraScene_init(void)
{
	hmbird_CameraScene_sysctl_init();
	return 0;
}

void hmbird_CameraScene_exit(void)
{
	hmbird_CameraScene_sysctl_deinit();
}