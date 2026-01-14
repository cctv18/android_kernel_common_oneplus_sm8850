/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024 Oplus. All rights reserved.
 */
#ifndef _HMBIRD_DFX_H_
#define _HMBIRD_DFX_H_

#define SHOW_STAT_BUF_SIZE  (3 * PAGE_SIZE)
#define SOCKET_INFO_MAX_NUMBER 10
#define MANAGER_INFO_MAX_NUMBER 10
#define SCENE_TYPE_MAX_LEN 32
#define SCENE_STATUS_MAX_LEN 12
#define BPF_STATUS_VALUE_MAX_LEN 10
#define ANDROID_PACKAGE_NAME_MAX_LEN 40
#define TIMESTAMP_LEN 25
#define KBUF_LEN 128
#define KBUF_MIN_INPUT_LEN 5

#define OPLUS_HMBIRD_PROC_DIR "oplus_hmbird"
#define SCENE_STAT_PROC "scene_stat"
#define COMPACT_SCENE_STAT_PROC "compact_scene_stat"
#define SOCKET_INFO_PROC "socket_info"
#define MANAGER_INFO_PROC "manager_info"
#define SUMMARY_PROC "summary"

#define NS_PER_SEC 1000000000ULL
#define MANAGER_HB_TIMEOUT_NSEC 2500000000ULL
#define MANAGER_HB_STAT_PROC "manager_hb_stat"
#define MANAGER_HB_FEED_PROC "manager_hb_feed"
#define MANAGER_PID_PROC "manager_pid"
#define GPA_PID_PROC "gpa_pid"
#define NOTIFY_SIGNAL SIGUSR1

enum SCENE_TYPES {
	SCENE_HMBIRD_II,
	SCENE_MULTIMEDIA,
	SCENE_CAMERA,
	SCENE_TYPES_COUNT,
};

enum SCENE_STATUS {
	BPF_STATUS,
	OPEN_RET_STATUS,
	LOAD_RET_STATUS,
	ATTACH_RET_STATUS,
	DETACH_RET_STATUS,
	DESTROY_RET_STATUS,
	SCENE_STATUS_COUNT,
};

enum BPF_STATUS_VALUE {
	BPF_STATUS_INIT,
	BPF_STATUS_OPEN,
	BPF_STATUS_LOAD,
	BPF_STATUS_ATTACH,
	BPF_STATUS_DETACH,
	BPF_STATUS_ERROR,
	BPF_STATUS_VALUE_COUNT,
};

enum RET_VAULE {
	RET_NO_RET,
	RET_SUCCESS,
	RET_NO_SKEL,
	RET_NO_FUNC,
	RET_NO_MAP_OPS,
	RET_NO_LINK,
	RET_ERROR,
	RET_VALUE_COUNT,
};

enum SOCKET_TYPE {
	SOCKET_RESET,
	SOCKET_HEARTBEAT,
	SOCKET_ATTACH,
	SOCKET_DETACH,
	SOCKET_UNKNOWN,
	SOCKET_TYPE_COUNT,
};

void notify_gpa_exit_hmbird(void);
void notify_manager_exit(void);

/* scene_stat structs & globals*/
struct scene_stat {
	atomic_t status[SCENE_STATUS_COUNT];
};

struct scene_stats {
	struct scene_stat per_scene_stat[SCENE_TYPES_COUNT];
};

struct scene_stat_snap {
	u64 status[SCENE_STATUS_COUNT];
};

struct scene_stats_snap {
	struct scene_stat_snap per_scene_stat[SCENE_TYPES_COUNT];
};

struct manager_info {
	char timestamp[TIMESTAMP_LEN];
	char scene[SCENE_TYPE_MAX_LEN];
	char action[SCENE_STATUS_MAX_LEN];
	char package_name[ANDROID_PACKAGE_NAME_MAX_LEN];
};

#define DFL_MGR_DUMP_LEN_U64 ((sizeof(struct manager_info) * MANAGER_INFO_MAX_NUMBER + sizeof(u64) - 1) / sizeof(u64))
#define DFL_MGR_DUMP_LEN (DFL_MGR_DUMP_LEN_U64 * sizeof(u64))

int hmbird_dfx_init(void);
void hmbird_dfx_exit(void);

#endif /* _HMBIRD_DFX_H_ */
