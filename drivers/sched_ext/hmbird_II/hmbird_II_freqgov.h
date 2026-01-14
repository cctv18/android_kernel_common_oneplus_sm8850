/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Oplus. All rights reserved.
 */
#ifndef _HMBIRD_II_FREQGOV_H_
#define _HMBIRD_II_FREQGOV_H_

#include <linux/cpufreq.h>
#include <uapi/linux/sched/types.h>
#include "hmbird_II_bpf.h"
#include "hmbird_II.h"

int hmbird_freqgov_init(void);
void gov_switch_state_systrace_c(void);

struct heavy_boost_params{
	int type;
	int bottom_perf;
	int boost_weight;
};

#endif /* _HMBIRD_II_FREQGOV_H_ */
