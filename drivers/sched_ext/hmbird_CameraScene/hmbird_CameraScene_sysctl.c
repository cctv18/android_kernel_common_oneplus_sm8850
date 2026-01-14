// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Oplus. All rights reserved.
 */
#include <linux/slab.h>
#include <linux/sysctl.h>
#include <linux/kmemleak.h>
#include <linux/ktime.h>
#include <linux/rwsem.h>
#include <linux/rbtree.h>
#include <linux/jiffies.h>
#include <linux/spinlock.h>

#include "hmbird_CameraScene.h"
#include "hmbird_common.h"

#define MAX_NR_PIPELINE		(8)
#define MSEC_PER_SEC		1000L
#define NSEC_PER_MSEC		1000000L
#define JIFFIES_MS			(MSEC_PER_SEC / HZ)
#define FRAME_MAX_UTIL		(1024)

#define CREATE_TRACE_POINTS
#include "trace_hmbird_CameraScene.h"

unsigned int g_vutil_time2max  = 30;
unsigned int g_frame_state     = 0;

static unsigned long key_info[2];
static unsigned int boost_threshold = (30 * NSEC_PER_MSEC);
static unsigned int jiffies_delay = 8;
int boost_enable = 0;
raw_spinlock_t pipeline_lock;
pipeline_status_t g_pipeline[MAX_NR_PIPELINE];

static inline unsigned int check_jiffies_delayed(unsigned long start_jiffies)
{
	if (jiffies - start_jiffies < jiffies_delay - 1)
		return PIPELINE_NDELAYED;
	else if (jiffies - start_jiffies > jiffies_delay)
		return PIPELINE_DELAYED;
	else
		return PIPELINE_SLOWPATH;
}

static unsigned int __try_check_pipeline_delayed_locked_fastpath(pipeline_status_t* pipeline)
{
	return check_jiffies_delayed(pipeline->start_jiffies);
}

static bool __check_pipeline_delayed_locked_slowpath(pipeline_status_t* pipeline)
{
	unsigned long now = ktime_get_ns();

	return (now >= pipeline->start_time + boost_threshold);
}

unsigned long check_pipeline_delayed_locked(void)
{
	unsigned long max_delayed = 0;

	for (int i = 0; i < MAX_NR_PIPELINE; i++) {
		if (!g_pipeline[i].finished && g_pipeline[i].stage == 0) {
			unsigned long flag = __try_check_pipeline_delayed_locked_fastpath(&g_pipeline[i]);

			if (flag == PIPELINE_NDELAYED)
				continue;
			else if (flag == PIPELINE_DELAYED)
				goto ascend;
			else {
				if (!__check_pipeline_delayed_locked_slowpath(&g_pipeline[i]))
					continue;
			}

ascend:
			g_pipeline[i].delayed_tick_count++;
			max_delayed = max(max_delayed, g_pipeline[i].delayed_tick_count);
		}
	}
	return max_delayed;
}

static void track_camera_pipeline_status_locked(int pipeline, int stage)
{
	unsigned long long now = ktime_get_ns();

	if (g_pipeline[pipeline].stage == 1 && stage == 0) {
		g_pipeline[pipeline].start_time = now;
		g_pipeline[pipeline].start_jiffies = jiffies;
		g_pipeline[pipeline].stage = 0;
		g_pipeline[pipeline].finished = false;
	} else if (g_pipeline[pipeline].stage == 0 && stage == 1) {
		g_pipeline[pipeline].stage = 1;
		g_pipeline[pipeline].finished = true;
		g_pipeline[pipeline].delayed_tick_count = 0;
	} else {
		if (printk_ratelimit())
			pr_err("hmbird camera boost err: pipeline[%d].stage:%llu, new stage:%d",
							pipeline, g_pipeline[pipeline].stage, stage);
	}
}

static int cfg_key_info_handler(const struct ctl_table *table,
				int write, void *buffer, size_t *lenp,
				loff_t *ppos)
{
	int ret = -EPERM;
	static int cached[2] = {-1, -1};
	int pipeline_and_state[2] = {-1, -1};
	char buf[32];
	struct ctl_table tmp = { };
	static DEFINE_MUTEX(mutex);

	if (!boost_enable)
		return ret;

	mutex_lock(&mutex);
	if (!write) {
		ret = scnprintf(buf, sizeof(buf), "%d | %d", cached[0], cached[1]);
		if (ret < 0)
			goto unlock;
		tmp.data = &buf;
		tmp.maxlen = sizeof(buf);
		ret = proc_dostring(&tmp, write, buffer, lenp, ppos);
	} else {
		int pipeline;
		unsigned long flag;
		cached[0] = -1;
		cached[1] = -1;
		tmp.data = &pipeline_and_state;
		tmp.maxlen = sizeof(pipeline_and_state);
		ret = proc_dointvec(&tmp, write, buffer, lenp, ppos);

		if (ret)
			goto unlock;

		pipeline = pipeline_and_state[1] % 8;

		raw_spin_lock_irqsave(&pipeline_lock, flag);
		track_camera_pipeline_status_locked(pipeline, pipeline_and_state[0]);
		raw_spin_unlock_irqrestore(&pipeline_lock, flag);

		if (!ret) {
			cached[0] = pipeline_and_state[0];
			cached[1] = pipeline_and_state[1];
		}
	}
unlock:
	mutex_unlock(&mutex);
	return ret;
}

static int crit_bias = 0;
static int cfg_crit_bias(const struct ctl_table *table,
			 int write, void *buffer, size_t *lenp,
			 loff_t *ppos)
{
	int ret = -EPERM;
	int val;
	static DEFINE_MUTEX(mutex);

	struct ctl_table tmp = {
		.data = &val,
		.maxlen = sizeof(val),
		.mode = table->mode,
	};

	mutex_lock(&mutex);
	if (!write) {
		val = crit_bias;
		ret = proc_dointvec(&tmp, write, buffer, lenp, ppos);
	} else {
		ret = proc_dointvec(&tmp, write, buffer, lenp, ppos);
		if (ret)
			goto unlock;
		crit_bias = val;
		trace_hmbird_crit_bias_update((unsigned int)val);
	}
unlock:
	mutex_unlock(&mutex);
	return ret;
}

static int synergy = 0;
static int cfg_synergy(const struct ctl_table *table,
				int write, void *buffer, size_t *lenp,
				loff_t *ppos)
{
	int ret = -EPERM;
	int val;
	static DEFINE_MUTEX(mutex);

	struct ctl_table tmp = {
		.data = &val,
		.maxlen = sizeof(val),
		.mode = table->mode,
	};

	mutex_lock(&mutex);
	if (!write) {
		val = synergy;
		ret = proc_dointvec(&tmp, write, buffer, lenp, ppos);
	} else {
		ret = proc_dointvec(&tmp, write, buffer, lenp, ppos);
		if (ret)
			goto unlock;
		synergy = val;
		trace_hmbird_synergy_update((unsigned int)val);
	}
unlock:
	mutex_unlock(&mutex);
	return ret;
}

static int eas_bias = 0;
static int cfg_eas_bias(const struct ctl_table *table,
			int write, void *buffer, size_t *lenp,
			loff_t *ppos)
{
	int ret = -EPERM;
	int val;
	static DEFINE_MUTEX(mutex);

	struct ctl_table tmp = {
		.data = &val,
		.maxlen = sizeof(val),
		.mode = table->mode,
	};

	mutex_lock(&mutex);
	if (!write) {
		val = eas_bias;
		ret = proc_dointvec(&tmp, write, buffer, lenp, ppos);
	} else {
		ret = proc_dointvec(&tmp, write, buffer, lenp, ppos);
		if (ret)
			goto unlock;
		eas_bias = val;
		trace_hmbird_eas_bias_update((unsigned int)val);
	}
unlock:
	mutex_unlock(&mutex);
	return ret;
}

static int core_ctrl = 1;
static int cfg_core_ctrl(const struct ctl_table *table,
			 int write, void *buffer, size_t *lenp,
			 loff_t *ppos)
{
	int ret = -EPERM;
	int val;
	static DEFINE_MUTEX(mutex);

	struct ctl_table tmp = {
		.data = &val,
		.maxlen = sizeof(val),
		.mode = table->mode,
	};

	mutex_lock(&mutex);
	if (!write) {
		val = core_ctrl;
		ret = proc_dointvec(&tmp, write, buffer, lenp, ppos);
	} else {
		ret = proc_dointvec(&tmp, write, buffer, lenp, ppos);
		if (ret)
			goto unlock;
		core_ctrl = val;
		trace_hmbird_core_ctrl_update((unsigned int)val);
	}
unlock:
	mutex_unlock(&mutex);
	return ret;
}

static int crit_ordi_ratio = 0;
static int cfg_crit_ordi_ratio(const struct ctl_table *table,
					int write, void *buffer, size_t *lenp,
					loff_t *ppos)
{
	int ret = -EPERM;
	int val;
	static DEFINE_MUTEX(mutex);

	struct ctl_table tmp = {
		.data = &val,
		.maxlen = sizeof(val),
		.mode = table->mode,
	};

	mutex_lock(&mutex);
	if (!write) {
		val = crit_ordi_ratio;
		ret = proc_dointvec(&tmp, write, buffer, lenp, ppos);
	} else {
		ret = proc_dointvec(&tmp, write, buffer, lenp, ppos);
		if (ret)
			goto unlock;
		crit_ordi_ratio = val;
		trace_hmbird_crit_ordi_ratio_update((unsigned int)val);
	}
unlock:
	mutex_unlock(&mutex);
	return ret;
}

static inline unsigned int u32_devide_roundup(unsigned int a, unsigned int b)
{
	return (a + b - 1) / b;
}

static int cfg_boost_threshold_handler(const struct ctl_table *table,
					int write, void *buffer, size_t *lenp,
					loff_t *ppos)
{
	int ret = -EPERM;
	unsigned int val;
	static DEFINE_MUTEX(mutex);

	struct ctl_table tmp = {
		.data = &val,
		.maxlen = sizeof(val),
		.mode = table->mode,
	};

	mutex_lock(&mutex);
	if (!write) {
		val = boost_threshold;
		ret = proc_dointvec(&tmp, write, buffer, lenp, ppos);
	} else {
		ret = proc_dointvec(&tmp, write, buffer, lenp, ppos);
		if (ret)
			goto unlock;
		boost_threshold = val;

		if (boost_threshold < 4 * NSEC_PER_MSEC)
			boost_threshold = 4 * NSEC_PER_MSEC;

		jiffies_delay = u32_devide_roundup(boost_threshold / NSEC_PER_MSEC, JIFFIES_MS);
	}
unlock:
	mutex_unlock(&mutex);
	return ret;
}

static int cfg_boost_enable_handler(const struct ctl_table *table,
					int write, void *buffer, size_t *lenp,
					loff_t *ppos)
{
	int ret = -EPERM;
	unsigned int val;
	static DEFINE_MUTEX(mutex);

	struct ctl_table tmp = {
		.data = &val,
		.maxlen = sizeof(val),
		.mode = table->mode,
	};

	mutex_lock(&mutex);
	if (!write) {
		val = boost_enable;
		ret = proc_dointvec(&tmp, write, buffer, lenp, ppos);
	} else {
		ret = proc_dointvec(&tmp, write, buffer, lenp, ppos);
		if (ret)
			goto unlock;
		boost_enable = val;
	}
unlock:
	mutex_unlock(&mutex);
	return ret;
}

static struct ctl_table hmbird_Camera_table[] = {
	{
		.procname	= "key_info",
		.data		= &key_info,
		.maxlen		= sizeof(unsigned long)*2,
		.mode		= 0666,
		.proc_handler	= cfg_key_info_handler,
	},
	{
		.procname	= "crit_bias",
		.data		= &crit_bias,
		.maxlen		= sizeof(int),
		.mode		= 0666,
		.proc_handler	= cfg_crit_bias,
	},
	{
		.procname	= "synergy",
		.data		= &synergy,
		.maxlen		= sizeof(int),
		.mode		= 0666,
		.proc_handler	= cfg_synergy,
	},
	{
		.procname	= "eas_bias",
		.data		= &eas_bias,
		.maxlen		= sizeof(int),
		.mode		= 0666,
		.proc_handler	= cfg_eas_bias,
	},
	{
		.procname	= "core_ctrl",
		.data		= &core_ctrl,
		.maxlen		= sizeof(int),
		.mode		= 0666,
		.proc_handler	= cfg_core_ctrl,
	},
	{
		.procname	= "crit_ordi_ratio",
		.data		= &crit_ordi_ratio,
		.maxlen		= sizeof(int),
		.mode		= 0666,
		.proc_handler	= cfg_crit_ordi_ratio,
	},
	{
		.procname	= "boost_threshold",
		.data		= &boost_threshold,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0666,
		.proc_handler	= cfg_boost_threshold_handler,
	},
	{
		.procname	= "boost_enable",
		.data		= &boost_enable,
		.maxlen		= sizeof(int),
		.mode		= 0666,
		.proc_handler	= cfg_boost_enable_handler,
	},
};

static void camera_boost_init(void)
{
	unsigned long flag;

	raw_spin_lock_init(&pipeline_lock);

	raw_spin_lock_irqsave(&pipeline_lock, flag);
	for (int j = 0; j < MAX_NR_PIPELINE; j++) {
		g_pipeline[j].stage = 1;
		g_pipeline[j].delayed_tick_count = 0;
		g_pipeline[j].start_time = 0;
		g_pipeline[j].finished = true;
	}
	raw_spin_unlock_irqrestore(&pipeline_lock, flag);
}

static struct ctl_table_header *hdr;
int hmbird_CameraScene_sysctl_init(void)
{
	camera_boost_init();

	hdr = register_sysctl("hmbird_Camera", hmbird_Camera_table);

	kmemleak_not_leak(hdr);
	return 0;
}

void hmbird_CameraScene_sysctl_deinit(void)
{
	unregister_sysctl_table(hdr);
}
