/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Oplus. All rights reserved.
 */


#ifndef _OPLUS_SA_OEMDATA_MGR_H_
#define _OPLUS_SA_OEMDATA_MGR_H_

#include <linux/sched.h>
#include "sa_common_struct.h"

/************************task_struct->android_oem_data *************************************/

enum TSK_OEM_DATA_TYPE {
	OTS_IDX = 0,
	/* refer to struct task_struct for oem data size */
	TSK_OEM_DATA_TYPE_MAX = 5,
};

static inline struct oplus_task_struct* get_oplus_task_struct(struct task_struct *t)
{
	struct oplus_task_struct *ots = NULL;

	/* not Skip idle thread */
	if (!t)
		return NULL;

	ots = (struct oplus_task_struct *) READ_ONCE(t->android_oem_data1[OTS_IDX]);
	if (IS_ERR_OR_NULL(ots))
		return NULL;

	return ots;
}

static inline void set_oplus_task_struct(struct task_struct *tsk, struct oplus_task_struct *ots)
{
	WRITE_ONCE(tsk->android_oem_data1[OTS_IDX], (u64) ots);
}


/************************rq->android_oem_data *************************************/
enum RQ_OEM_DATA_TYPE {
	ORQ_IDX = 0,
	/* oem data max size */
	RQ_OEM_DATA_TYPE_MAX = 15,
};


/*use macro instead function to avoid compile error*/
#define set_oplus_rq(rq, orq) \
	do {	\
		WRITE_ONCE(rq->android_oem_data1[ORQ_IDX], (u64) orq);	\
	} while (0)

#endif /* _OPLUS_SA_OEMDATA_MGR_H_ */
