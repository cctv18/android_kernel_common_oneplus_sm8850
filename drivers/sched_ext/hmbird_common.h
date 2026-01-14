/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Oplus. All rights reserved.
 */
#ifndef _HMBIRD_COMMON_H_
#define _HMBIRD_COMMON_H_

#include <linux/sa_common.h>

extern struct task_struct *sched_ext_helper;
static inline struct scx_entity *get_oplus_ext_entity(struct task_struct *p)
{
	struct oplus_task_struct *ots = get_oplus_task_struct(p);
	if (!ots) {
		WARN_ONCE(1, "scx_sched_ext:get_oplus_ext_entity NULL!");
		return NULL;
	}
	return &ots->scx;
}

#endif /* _HMBIRD_COMMON_H_ */
