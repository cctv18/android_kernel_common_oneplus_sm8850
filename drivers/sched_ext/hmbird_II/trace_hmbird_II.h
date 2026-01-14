/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024 Oplus. All rights reserved.
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM hmbird_II

#if !defined(_TRACE_HMBIRD_II_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HMBIRD_II_H

#include <linux/sched.h>
#include <linux/types.h>
#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(hmbird_cfg_val,

	TP_PROTO(unsigned int val),

	TP_ARGS(val),

	TP_STRUCT__entry(
		__field(unsigned int, val)),

	TP_fast_assign(
		__entry->val = val;),

	TP_printk("val=%u",
		__entry->val)
);

DEFINE_EVENT(hmbird_cfg_val, hmbird_debug_update,
	TP_PROTO(unsigned int hmbird_debug),
	TP_ARGS(hmbird_debug));

DEFINE_EVENT(hmbird_cfg_val, hmbird_frame_update,
	TP_PROTO(unsigned int frame),
	TP_ARGS(frame));

TRACE_EVENT(hmbird_cfg_coefficient,

	TP_PROTO(int cluster_id, unsigned long coefficient),

	TP_ARGS(cluster_id, coefficient),

	TP_STRUCT__entry(
		__field(int, cluster_id)
		__field(unsigned long, coefficient)),

	TP_fast_assign(
		__entry->cluster_id = cluster_id;
		__entry->coefficient = coefficient;),

	TP_printk("cfg_coefficient:cluster=%d, coefficient=%lu",
		__entry->cluster_id, __entry->coefficient)
);

TRACE_EVENT(hmbird_cfg_perf_high_ratio,

	TP_PROTO(int cluster_id, unsigned long perf_high_ratio),

	TP_ARGS(cluster_id, perf_high_ratio),

	TP_STRUCT__entry(
		__field(int, cluster_id)
		__field(unsigned long, perf_high_ratio)),

	TP_fast_assign(
		__entry->cluster_id = cluster_id;
		__entry->perf_high_ratio = perf_high_ratio;),

	TP_printk("cfg_perf_high_ratio:cluster=%d, perf_high_ratio=%lu",
		__entry->cluster_id, __entry->perf_high_ratio)
);

TRACE_EVENT(hmbird_cfg_freq_policy,

	TP_PROTO(int cluster_id, unsigned long freq_policy),

	TP_ARGS(cluster_id, freq_policy),

	TP_STRUCT__entry(
		__field(int, cluster_id)
		__field(unsigned long, freq_policy)),

	TP_fast_assign(
		__entry->cluster_id = cluster_id;
		__entry->freq_policy = freq_policy;),

	TP_printk("cfg_freq_policy:cluster=%d, freq_policy=%lu",
		__entry->cluster_id, __entry->freq_policy)
);

#endif /*_TRACE_HMBIRD_II_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ./hmbird_II

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace_hmbird_II
/* This part must be outside protection */
#include <trace/define_trace.h>
