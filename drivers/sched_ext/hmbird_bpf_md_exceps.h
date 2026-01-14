/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * This file only defines some basic data types and macros to ensure
 * consistency between BPF and ko.
 * Copyright (C) 2025 Oplus. All rights reserved.
 */
#ifndef _HMBIRD_BPF_MD_EXCEPS_H_
#define _HMBIRD_BPF_MD_EXCEPS_H_

#define MAX_NR_DSQS_MD 64

enum bpf_excep_id {
	CLUS_NR_ERR,
	CLUS_ID_ERR,
	CPU_ID_ERR,
	CRE_DSQS_FAIL,
	DSQ_ID_ERR,
	DSQ_CTX_ERR,
	DSQ_MAP_WEIGHT_ERR,
	CUMU_LOAD_ERR,
	TASK_CTX_ERR,
	TASK_TRACK_ERR,
	TASK_TRACK_CLUS_ERR,
	TASK_WIN_STR_ERR,
	TICK_CLOCK_ERR,
	WORK_TIMER_ERR,
	EXCLUSIVE_MSK_ERR,
	RESERVED_MSK_ERR,
	CPU_CTX_ERR,
	MAX_BPF_EXCEP_ID,
};

#endif /* _HMBIRD_BPF_MD_EXCEPS_H_ */
