#ifndef _HMBIRD_II_SYSCTL_H_
#define _HMBIRD_II_SYSCTL_H_

#define RESULT_PAGE_SIZE	1024

struct sched_prop_map {
	u64 sched_prop;
	u64 sched_prop_mask;
};

struct critical_task_params {
	int param_nums;
	int type;
	pid_t pid;
	struct sched_prop_map sched_map;
	char name[TASK_COMM_LEN];
};

enum cfg_task_sched_prop {
	PREFER_IDLE,
	PREFER_PREEMPT,
	PREFER_CLUSTER,
	PREFER_CPU,
	SET_UCLAMP,
	UCLAMP_KEEP_FREQ,
	SCHED_PROP_DIRECTLY,
	MAX_NR_CFG_TYPE
};

#endif /*_HMBIRD_II_SYSCTL_H_*/
