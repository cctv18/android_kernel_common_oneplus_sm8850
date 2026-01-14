#include <linux/module.h>
#include <linux/sched.h>
#include <linux/types.h>

int global_sched_assist_enabled;

static void (*g_ttwu_entry_func)(struct task_struct *task) = NULL;

int set_ttwu_callback(void (*entry_func)(struct task_struct *task))
{
	if (NULL != entry_func)
		g_ttwu_entry_func = entry_func;
	return 0;
}

EXPORT_SYMBOL(global_sched_assist_enabled);
EXPORT_SYMBOL_GPL(set_ttwu_callback);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Oplus BSP Stub Symbols");
