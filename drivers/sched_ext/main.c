// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Oplus. All rights reserved.
 */
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kprobes.h>
#include "hmbird_CameraScene/hmbird_CameraScene.h"
#include "hmbird_II/hmbird_II.h"
#include "hmbird_kfunc.h"
#include "hmbird_parse_dts.h"
#include "hmbird_dfx.h"

extern int hmbird_common_sysctl_init(void);
extern void hmbird_common_sysctl_deinit(void);
extern void hmbird_minidump_init(void);
extern void hmbird_minidump_exit(void);

#ifdef CONFIG_OPLUS_SYSTEM_KERNEL_MTK
int (*addr_get_vip_task_prio)(struct task_struct *p);
LOOKUP_KERNEL_SYMBOL(get_vip_task_prio);
int hmbird_common_lookup_symbols(void)
{
	return lookup_get_vip_task_prio();
}
#else
int hmbird_common_lookup_symbols(void) { return 0; }
#endif

static int __init hmbird_common_init(void)
{
	if (!(HMBIRD_EXT == get_hmbird_config_type())) {
		pr_info("hmbird: sched_ext[init] no config dts.\n");
		return 0;
	}
	hmbird_common_lookup_symbols();
	pre_hmbird_kfunc_register();
	hmbird_kfunc_register();
	hmbird_common_sysctl_init();
	hmbird_II_init();
	hmbird_CameraScene_init();
	hmbird_dfx_init();
	hmbird_minidump_init();
	return 0;
}

static void __exit hmbird_common_exit(void)
{
	if (!(HMBIRD_EXT == get_hmbird_config_type())) {
		pr_info("hmbird: sched_ext[exit] no config dts.\n");
		return;
	}
	hmbird_common_sysctl_deinit();
	hmbird_II_exit();
	hmbird_CameraScene_exit();
	hmbird_dfx_exit();
	hmbird_minidump_exit();
}

module_init(hmbird_common_init);
module_exit(hmbird_common_exit);
MODULE_LICENSE("GPL v2");
