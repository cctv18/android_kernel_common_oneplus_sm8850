/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * This file only defines some basic data types and macros to ensure
 * consistency between BPF and ko.
 * Copyright (C) 2025 Oplus. All rights reserved.
 */
#ifndef _HMBIRD_MINIDUMP_H_
#define _HMBIRD_MINIDUMP_H_
#include "hmbird_bpf_md_exceps.h"

/* NOTING : Must align to 64bits. */
#define DESC_STR_LEN	(32)
/* Supporti up to  three-dimensional arrays. */
#define PARSE_DIMENS	(3)
#define CHAR_UNIT_TYPE	('c')
#define U64_UNIT_TYPE	('Q')
#define MAX_SWITCHS	(5)

enum hmbird_switch_reason_type {
	HMBIRD_SWITCH_NORMAL,
	HMBIRD_SWITCH_PROC,
	HMBIRD_SWITCH_ERR_WDT,
	HMBIRD_SWITCH_ERR_HB,
	HMBIRD_SWITCH_ERR_DSQ,
	HMBIRD_EXIT_ERROR_STALL,	/* watchdog detected stalled runnable tasks */
	HMBIRD_EXIT_ERROR_HEARTBEAT,	/* heart beat has stopped */
};

enum switch_end_stat {
	HMBIRD_DISABLED,
	HMBIRD_ENABLING,
	HMBIRD_SWITCH_PREP,
	HMBIRD_RQ_SWITCH_BEGIN,
	HMBIRD_RQ_SWITCH_DONE,
	HMBIRD_ENABLED,
	HMBIRD_DISABLING,
	HMBIRD_DISABLED_WITHOUT_ING,
};

#define MAX_EXCEPS	(5)
enum ko_excep_id {
	KO_DEINITED,
	MAX_KO_EXCEP_ID,
};

struct md_meta_t {
	u64 self_md_meta_size;
	u64 dump_real_size;
	u64 desc_meta_size;
	u64 desc_str_size;
	u64 desc_parse_dimens;
	u64 desc_meta_nr;
	u64 switches;
	u64 exceps;
	u64 nr_cpus;
	u64 real_cpus;
};

struct meta_desc_t {
	char desc_str[DESC_STR_LEN];
	u64 desc_len_u64;
	u64 unit_type;
	u64 each_dimen_len[PARSE_DIMENS];
};

struct hmbird_switch_t {
	u64 switch_at;
	u64 is_success;
	u64 end_state;
	u64 switch_reason;
};
#define SWITCH_ITEMS	(sizeof(struct hmbird_switch_t) / sizeof(u64))

extern void sw_update(u64 is_success, u64 end_state, u64 switch_reason);
extern void ko_exceps_update(int id, unsigned long jiffies);
extern void bpf_exceps_update(int id, unsigned long jiffies);
extern void bpf_snapshot_misc_update(int nr_dsq, int nr_aval_clus);
extern void bpf_snapshot_dsq_update(u64 easy_dsq_id, u64 runnable_at, u64 dsq_timeout);

#endif /* _HMBIRD_MINIDUMP_H_ */
