/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024 Oplus. All rights reserved.
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM hmbird_CameraScene

#if !defined(_TRACE_HMBIRD_CAMERASCENE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HMBIRD_CAMERASCENE_H

#include <linux/sched.h>
#include <linux/types.h>
#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(hmbird_camerascene_val,

	TP_PROTO(unsigned int val),

	TP_ARGS(val),

	TP_STRUCT__entry(
	__field(unsigned int, val)),

	TP_fast_assign(
	__entry->val = val;),

TP_printk("val=%u",
__entry->val)
);

DEFINE_EVENT(hmbird_camerascene_val, hmbird_crit_bias_update,
	TP_PROTO(unsigned int crit_bias),
	TP_ARGS(crit_bias));

DEFINE_EVENT(hmbird_camerascene_val, hmbird_synergy_update,
	TP_PROTO(unsigned int synergy),
	TP_ARGS(synergy));

DEFINE_EVENT(hmbird_camerascene_val, hmbird_eas_bias_update,
	TP_PROTO(unsigned int eas_bias),
	TP_ARGS(eas_bias));

DEFINE_EVENT(hmbird_camerascene_val, hmbird_core_ctrl_update,
	TP_PROTO(unsigned int core_ctrl),
	TP_ARGS(core_ctrl));

DEFINE_EVENT(hmbird_camerascene_val, hmbird_crit_ordi_ratio_update,
	TP_PROTO(unsigned int crit_ordi_ratio),
	TP_ARGS(crit_ordi_ratio));

#endif /*_TRACE_HMBIRD_CAMERASCENE_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH hmbird_CameraScene

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace_hmbird_CameraScene
/* This part must be outside protection */
#include <trace/define_trace.h>
