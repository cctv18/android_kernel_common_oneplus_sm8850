/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Since the BTF information for the kernel modules (ko) cannot be
 * automatically generated in vmlinux.h during compilation and macro
 * definitions cannot be resolved into BTF information, a shared header
 * file for ko and the BPF program is required.
 * This file only defines some basic data types and macros to ensure
 * consistency between BPF and ko.
 * Copyright (C) 2025 Oplus. All rights reserved.
 */
#ifndef _HMBIRD_II_BPF_H_
#define _HMBIRD_II_BPF_H_

#ifndef UL
#define UL(x)	((unsigned long)(x))
#endif

#ifndef BIT
#define BIT(nr)	(UL(1) << (nr))
#endif

/*sched_prop*/
#define HMBIRD_SCHED_PROP_PREFER_CLUSTER_MASK   (0x7f)
#define HMBIRD_SCHED_PROP_PREFER_CLUSTER_SET    BIT(7)
#define HMBIRD_SCHED_PROP_PREFER_CPU_SHIFT      (8)
#define HMBIRD_SCHED_PROP_PREFER_CPU_MASK       (0xffUL << HMBIRD_SCHED_PROP_PREFER_CPU_SHIFT)
#define HMBIRD_SCHED_PROP_PREFER_CPU_SET        BIT(23)
#define HMBIRD_SCHED_PROP_DSQ_SHIFT             (24)
#define HMBIRD_SCHED_PROP_DSQ_IDX_MASK          (0xffUL << HMBIRD_SCHED_PROP_DSQ_SHIFT)
#define HMBIRD_SCHED_PROP_PREFER_PREEMPT        BIT(32)
#define HMBIRD_SCHED_PROP_PREFER_IDLE           BIT(33)
#define HMBIRD_SCHED_PROP_SET_UCLAMP            BIT(34)
#define HMBIRD_SCHED_PROP_UCLAMP_KEEP_FREQ      BIT(35)
#define HMBIRD_SCHED_PROP_UCLAMP_VAL_SHIFT      (36)
#define HMBIRD_SCHED_PROP_UCLAMP_VAL_MASK       (0x7ffUL << HMBIRD_SCHED_PROP_UCLAMP_VAL_SHIFT)

#define HMBIRD_CPUFREQ_PERF_FLAG_MASK        0x7
#define HMBIRD_CPUFREQ_WINDOW_ROLLOVER_FLAG  1
#define HMBIRD_CPUFREQ_HEAVY_BOOST_FLAG      2
#define HMBIRD_TASK_UCLAMP_UPDATE_FLAG       3
#define HMBIRD_CPUFREQ_TOP_TASK_BOOST_FLAG   4
#define HMBIRD_CPUFREQ_CAMREA_BOOST_FLAG     5

#endif /* _HMBIRD_II_BPF_H_ */
