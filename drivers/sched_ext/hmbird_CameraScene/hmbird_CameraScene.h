#ifndef _HMBIRD_CAMERA_SCENE_H_
#define _HMBIRD_CAMERA_SCENE_H_

typedef struct pipeline_status {
	unsigned long long start_time;
	unsigned long start_jiffies;
	unsigned long long stage;
	bool finished;
	unsigned long delayed_tick_count;
} pipeline_status_t;

enum {
	PIPELINE_DELAYED = 0,
	PIPELINE_NDELAYED,
	PIPELINE_SLOWPATH,
};

extern raw_spinlock_t pipeline_lock;
extern pipeline_status_t g_pipeline[];
extern int boost_enable;

unsigned long check_pipeline_delayed_locked(void);
int hmbird_CameraScene_sysctl_init(void);
void hmbird_CameraScene_sysctl_deinit(void);

int hmbird_CameraScene_init(void);
void hmbird_CameraScene_exit(void);
#endif  /* _HMBIRD_CAMERA_SCENE_H_ */
