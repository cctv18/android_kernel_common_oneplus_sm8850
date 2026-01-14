// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Oplus. All rights reserved.
 */
#include <linux/slab.h>
#include <linux/sysctl.h>
#include <linux/kmemleak.h>
#include <linux/ktime.h>

#define CREATE_TRACE_POINTS
#include "trace_hmbird_common.h"

enum {
	HMBIRD_BPF_LOG_OFF_LEVEL = 0,
	HMBIRD_BPF_LOG_CRITICAL_LEVEL = 1,
	HMBIRD_BPF_LOG_ERR_LEVEL = 2,
	HMBIRD_BPF_LOG_WARN_LEVEL = 3,
	HMBIRD_BPF_LOG_INFO_LEVEL = 4,
	HMBIRD_BPF_LOG_DBG_LEVEL = 5,
	HMBIRD_BPF_LOG_LEVEL_MAX = HMBIRD_BPF_LOG_DBG_LEVEL
};

unsigned int hmbird_bpf_log_level = HMBIRD_BPF_LOG_ERR_LEVEL;

static int cfg_hmbird_bpf_log_level(const struct ctl_table *table, int write, void *buffer, size_t *lenp, loff_t *ppos) {
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
		val = hmbird_bpf_log_level;
		ret = proc_dointvec(&tmp, write, buffer, lenp, ppos);
	} else {
		ret = proc_dointvec(&tmp, write, buffer, lenp, ppos);
		if (ret) {
			pr_warn("hmbird_II: common_sysctl set bpf_log_level failed, ret %d\n", ret);
			goto unlock;
		}

		if (val < HMBIRD_BPF_LOG_OFF_LEVEL || val > HMBIRD_BPF_LOG_LEVEL_MAX) {
			ret = -EINVAL;
			pr_warn("hmbird_II: common_sysctl get illegal LOG_LEVEL %d\n", val);
			goto unlock;
		}

		hmbird_bpf_log_level = val;
		trace_hmbird_bpf_log_level_update(hmbird_bpf_log_level);
	}

unlock:
	mutex_unlock(&mutex);
	return ret;
}

static struct task_struct *oplus_hmbird_bpf_manager = NULL;
struct task_struct *sched_ext_helper = NULL;
#define OPLUS_HMBIRD_BPF_MANAGER_PRIO			10
static int hmbird_bpf_manager_register(const struct ctl_table *table, int write, void *buffer, size_t *lenp, loff_t *ppos)
{
	int ret = -EPERM;
	int pid = -1;
	struct task_struct *manager;
	struct sched_param param = { .sched_priority = MAX_RT_PRIO - 1 - OPLUS_HMBIRD_BPF_MANAGER_PRIO};
	static DEFINE_MUTEX(mutex);

	struct ctl_table tmp = {
		.data	= &pid,
		.maxlen	= sizeof(pid),
		.mode	= table->mode,
	};

	mutex_lock(&mutex);

	if (write) {
		ret = proc_dointvec(&tmp, write, buffer, lenp, ppos);
		if (ret) {
			ret = -EFAULT;
			goto unlock;
		}
		manager = get_pid_task(find_vpid(pid), PIDTYPE_PID);
		if (!manager) {
			ret = ESRCH;
			pr_err("hmbird_bpf_manager_register::No such process\n");
			goto unlock;
		}

		if (manager == oplus_hmbird_bpf_manager) {
			pr_err("hmbird_bpf_manager_register::already registered\n");
			put_task_struct(manager);
			goto unlock;
		}

		ret = sched_setscheduler_nocheck(manager, SCHED_FIFO, &param);
		if (ret) {
			pr_err("hmbird_bpf_manager_register::sched_setscheduler_nocheck fail\n");
			put_task_struct(manager);
			goto unlock;
		}

		if (oplus_hmbird_bpf_manager) {
			put_task_struct(oplus_hmbird_bpf_manager);
			oplus_hmbird_bpf_manager = NULL;
		}
		oplus_hmbird_bpf_manager = manager;
	} else {
		char buf[32];
		if (oplus_hmbird_bpf_manager)
			snprintf(buf, sizeof(buf), "%s %d\n", oplus_hmbird_bpf_manager->comm, oplus_hmbird_bpf_manager->pid);
		else
			snprintf(buf, sizeof(buf), "none\n");
		tmp.data = buf;
		tmp.maxlen = sizeof(buf);
		ret = proc_dostring(&tmp, write, buffer, lenp, ppos);
	}

unlock:
	mutex_unlock(&mutex);
	return ret;
}


struct ctl_table hmbird_common_table[] = {
	{
		.procname	= "hmbird_bpf_log_level",
		.data		= &hmbird_bpf_log_level,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= cfg_hmbird_bpf_log_level,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_INT_MAX,
	},
	{
		.procname	= "hmbird_manager_register",
		.data		= &oplus_hmbird_bpf_manager,
		.maxlen		= sizeof(unsigned long),
		.mode		= 0644,
		.proc_handler	= hmbird_bpf_manager_register,
	}
};

static struct ctl_table_header *hdr;
int hmbird_common_sysctl_init(void)
{
	hdr = register_sysctl("hmbird/common", hmbird_common_table);

	kmemleak_not_leak(hdr);
	return 0;
}

void hmbird_common_sysctl_deinit(void)
{
	unregister_sysctl_table(hdr);
}
