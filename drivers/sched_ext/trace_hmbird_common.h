/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024 Oplus. All rights reserved.
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM hmbird_common

#if !defined(_TRACE_HMBIRD_COMMON_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HMBIRD_II_H

#include <linux/sched.h>
#include <linux/types.h>
#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(hmbird_common_val,

	TP_PROTO(unsigned int val),

	TP_ARGS(val),

	TP_STRUCT__entry(
	__field(unsigned int, val)),

	TP_fast_assign(
	__entry->val = val;),

TP_printk("val=%u",
__entry->val)
);

DEFINE_EVENT(hmbird_common_val, hmbird_bpf_log_level_update,
	TP_PROTO(unsigned int hmbird_bpf_log_level),
	TP_ARGS(hmbird_bpf_log_level));

TRACE_EVENT(hmbird_minidump_snapshot,

	TP_PROTO(int is_trigger),

	TP_ARGS(is_trigger),

	TP_STRUCT__entry(
		__field(int, is_trigger)),

	TP_fast_assign(
		__entry->is_trigger = is_trigger;),

	TP_printk("minidump_snapshot:is_trigger=%d",
		__entry->is_trigger)
);

#endif /*_TRACE_HMBIRD_COMMON_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace_hmbird_common
/* This part must be outside protection */
#include <trace/define_trace.h>
