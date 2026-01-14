// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Oplus. All rights reserved.
 */

#ifndef __CRITICAL_TASK_MONITOR_H__
#define __CRITICAL_TASK_MONITOR_H__

#define SF_NAME "surfaceflinger"

#define CTM_INFO(fmt, ...)	\
{				\
	pr_info("critical_task_monitor: [%s][%d]: "fmt, __func__, __LINE__, ##__VA_ARGS__);	\
}

enum ctm_type_flag {
	CTM_DISABLE_FLAG,
	APP_MAIN_TGID,
	SF_TYPE,
	SYS_UI_MAIN,
	CTM_TYPE_MAX
};

enum critical_task_type {
	DISABLE,
	CRITICAL_APP_TASK,
	CRITICAL_SYSUI_TASK,
	CRITICAL_SF_TASK,
	MAX_CRITICAL_TASK_TYPE
};

enum policy_cfged_flag {
	NOT_CONFIG,
	CONFIGED,
	NEED_CANCEL_CONFIG,
	NEED_REMOVE,
	MAX_NR_CFG_FLAG_TYPE
};

int critical_task_monitor_init(void);
int critical_task_monitor_deinit(void);

#endif /*__CRITICAL_TASK_MONITOR_H__*/

