// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Oplus. All rights reserved.
 */

#include <linux/atomic.h>
#include <linux/errno.h>
#include <linux/gfp.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pid.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/sched/signal.h>
#include <linux/seq_file.h>
#include <linux/signal.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/timekeeping.h>
#include <linux/uaccess.h>

#include "hmbird_dfx.h"

#ifdef DEBUG_LOG_ENABLE
#define hmbird_pr_debug(fmt, ...) \
	pr_info("[hmbird_dfx]: " fmt, ##__VA_ARGS__)
#else
#define hmbird_pr_debug(fmt, ...) \
	do { } while (0)
#endif

#define hmbird_pr_info(fmt, ...) \
	pr_info("[hmbird_dfx]: " fmt, ##__VA_ARGS__)

#define hmbird_pr_warn(fmt, ...) \
	pr_warn("[hmbird_dfx]: " fmt, ##__VA_ARGS__)

#define hmbird_pr_err(fmt, ...) \
	pr_err("[hmbird_dfx]: " fmt, ##__VA_ARGS__)


/* str */
static const char *scene_str[SCENE_TYPES_COUNT] = {
	"hmbird_II",
	"multimedia",
	"CameraScene"
};

static const char *status_str[SCENE_STATUS_COUNT] = {
	"status",
	"open_ret",
	"load_ret",
	"attach_ret",
	"detach_ret",
	"destory_ret"
};

static const char *bpf_status_value_str[BPF_STATUS_VALUE_COUNT] = {
	"init",
	"open",
	"load",
	"attach",
	"detach",
	"error"
};

static const char *socket_type_str[SOCKET_TYPE_COUNT] = {
	"reset",
	"heartbeat",
	"attach",
	"detach",
	"unknown"
};

static struct scene_stats g_scene_stat;

/* socket_info structs & globals*/
static int g_socket_info_head = 0;
static int g_socket_info_full = 0;

struct socket_info {
	char timestamp[TIMESTAMP_LEN];
	u32 socket;
};

struct socket_info g_socket_info[SOCKET_INFO_MAX_NUMBER];

/* manager_info structs & globals*/
static int g_manager_info_head = 0;
static int g_manager_info_full = 0;

struct manager_info g_manager_info[MANAGER_INFO_MAX_NUMBER];

/* heartbeat structs & globals */
enum hb_state {
	HB_IDLE = 0,
	HB_RUNNING = 1,
	HB_TIMEOUT = 2,
};

enum hb_app {
	HB_MANAGER = 0,
	HB_GPA = 1,
};

struct hb_ctx {
	struct mutex  lock;

	enum hb_app   app;
	enum hb_state state;
	pid_t         pid;

	unsigned long start_jiffies;
	unsigned long last_feed_jiffies;
	unsigned long last_timeout_jiffies;
	unsigned long last_stop_jiffies;

	unsigned long start_count;
	unsigned long stop_count;
	unsigned long timeout_restart_count;
	unsigned long timeout_count;
	unsigned long feed_count;

	struct        delayed_work timeout_work;
	u64  timeout_nsec;
};

static struct hb_ctx *g_manager_hb;

struct gpa_ctx {
	struct mutex  lock;
	pid_t         pid;
};

static struct gpa_ctx *g_gpa_hb;

static int scene_stat_show(struct seq_file *m, void *v)
{
	char *buf;
	int i, ret, idx = 0;

	struct scene_stat *scene_stat_info;

	buf = (char*)kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = snprintf(&buf[idx], (PAGE_SIZE - idx), "%-15s%-15s%-12s%-12s%-12s%-12s%-12s\n",
			" ", "bpf_status", "open_ret", "load_ret", "attach_ret", "detach_ret", "destory_ret");

	if ((ret < 0) || (ret >= PAGE_SIZE - idx))
		goto err;
	idx += ret;

	for (i = 0; i < SCENE_TYPES_COUNT; i++) {
		scene_stat_info = &g_scene_stat.per_scene_stat[i];
		int bpf_status_value = atomic_read(&scene_stat_info->status[BPF_STATUS]);
		char bpf_status_str[BPF_STATUS_VALUE_MAX_LEN];
		sprintf(bpf_status_str, "%d (%s)", bpf_status_value, bpf_status_value_str[bpf_status_value]);

		ret = snprintf(&buf[idx], (PAGE_SIZE - idx), "%-15s%-15s%-12d%-12d%-12d%-12d%-12d\n",
				scene_str[i],
				bpf_status_str,
				atomic_read(&scene_stat_info->status[OPEN_RET_STATUS]),
				atomic_read(&scene_stat_info->status[LOAD_RET_STATUS]),
				atomic_read(&scene_stat_info->status[ATTACH_RET_STATUS]),
				atomic_read(&scene_stat_info->status[DETACH_RET_STATUS]),
				atomic_read(&scene_stat_info->status[DESTROY_RET_STATUS]));

		if ((ret < 0) || (ret >= PAGE_SIZE - idx))
			goto err;
		idx += ret;
	}

	buf[idx] = '\0';
	seq_printf(m, "%s\n", buf);
	kfree(buf);

	return 0;

err:
	kfree(buf);
	return -EFAULT;
}

static int scene_stat_open(struct inode *inode, struct file *file)
{
	return single_open(file, scene_stat_show, inode);
}

static ssize_t scene_stat_write(struct file *file,
				const char __user * user_buffer, size_t count,
				loff_t * offset)
{
	char kbuf[KBUF_LEN];
	char scene[SCENE_TYPE_MAX_LEN], status[SCENE_STATUS_MAX_LEN];
	int ret, i, j;
	u32 val;
	struct scene_stat *scene_stat_info;

	if (count < KBUF_MIN_INPUT_LEN || count >= KBUF_LEN) {
		hmbird_pr_err("Invailid argument: count = %zu\n", count);
		return -EINVAL;
	}

	if (copy_from_user(kbuf, user_buffer, count)) {
		hmbird_pr_err("Copy from user failed\n");
		return -EFAULT;
	}

	kbuf[count] = '\0';

	ret = sscanf(kbuf, "%31s %11s %d", scene, status, &val);
	if (ret != 3) {
		hmbird_pr_err("Failed to execute sscanf\n");
		return -EFAULT;
	}

	for (i = 0; i < SCENE_TYPES_COUNT; i++) {
		if (strcmp(scene, scene_str[i]) == 0) {
			break;
		}
	}
	if (i == SCENE_TYPES_COUNT) {
		hmbird_pr_err("Invalid scene name\n");
		return -EFAULT;
	}

	for (j = 0; j < SCENE_STATUS_COUNT; j++) {
		if (strcmp(status, status_str[j]) == 0) {
			break;
		}
	}
	if (j == SCENE_STATUS_COUNT) {
		hmbird_pr_err("Invalid status name\n");
		return -EFAULT;
	}

	hmbird_pr_debug("writing %s - %s = %d\n", scene, status, val);
	scene_stat_info = &g_scene_stat.per_scene_stat[i];
	atomic_set(&scene_stat_info->status[j], val);

	return count;
}

static const struct proc_ops scene_stat_fops = {
	.proc_open	= scene_stat_open,
	.proc_write	= scene_stat_write,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
};

static int show_stats(char *buf, u32 blen)
{
	int i;
	int idx = 0;
	int ret;

	struct scene_stat *scene_stat_info;


	for (i = 0; i < SCENE_TYPES_COUNT; i++) {
		if (i == 0) {
				ret = snprintf(&buf[idx], (blen - idx), "{");
			if ((ret < 0) || (ret >= blen - idx))
				goto err;
			idx += ret;
		}

		scene_stat_info = &g_scene_stat.per_scene_stat[i];

		ret = snprintf(&buf[idx], (blen - idx), "%s:[%u,%d,%d,%d,%d,%d]",
				scene_str[i],
				atomic_read(&scene_stat_info->status[BPF_STATUS]),
				atomic_read(&scene_stat_info->status[OPEN_RET_STATUS]),
				atomic_read(&scene_stat_info->status[LOAD_RET_STATUS]),
				atomic_read(&scene_stat_info->status[ATTACH_RET_STATUS]),
				atomic_read(&scene_stat_info->status[DETACH_RET_STATUS]),
				atomic_read(&scene_stat_info->status[DESTROY_RET_STATUS]));

		if ((ret < 0) || (ret >= blen - idx))
			goto err;
		idx += ret;

		if ((SCENE_TYPES_COUNT - 1) != i) {
			ret = snprintf(&buf[idx], (blen - idx), ",");
			if ((ret < 0) || (ret >= blen - idx))
				goto err;
			idx += ret;
		}
	}
	ret = snprintf(&buf[idx], (blen - idx), "}\n");
	if ((ret < 0) || (ret >= blen - idx))
		goto err;
	idx += ret;

	buf[idx] = '\0';

	return 0;

err:
	return -EFAULT;
}

int scene_stats_dump(struct scene_stats_snap *m_scene_stats_snap)
{
	int i, j;

	for (i = 0; i < SCENE_TYPES_COUNT; i++) {
		for (j = 0; j < SCENE_STATUS_COUNT - 1; j++) {
			m_scene_stats_snap->per_scene_stat[i].status[j] =
				atomic_read(&g_scene_stat.per_scene_stat[i].status[j]);
		}
	}
	return 0;
}

static int compact_scene_stat_show(struct seq_file *m, void *v)
{
	char *buf;
	int ret;

	buf = kmalloc(SHOW_STAT_BUF_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = show_stats(buf, SHOW_STAT_BUF_SIZE);
	if (ret < 0) {
		kfree(buf);
		return -EFAULT;
	}

	seq_printf(m, "%s\n", buf);
	kfree(buf);

	return 0;
}

static int compact_scene_stat_open(struct inode *inode, struct file *file)
{
	return single_open(file, compact_scene_stat_show, inode);
}

static const struct proc_ops compact_scene_stat_fops = {
	.proc_open	= compact_scene_stat_open,
	.proc_write	= scene_stat_write,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
};

static void u32_to_bin_str(u32 val, char *buf)
{
	int i, j = 0;

	for (i = 31; i >= 0; --i) {
		buf[j++] = (val & (1U << i)) ? '1' : '0';
		if (i % 8 == 0 && i != 0)
			buf[j++] = ' ';
	}

	buf[j] = '\0';
}

static enum SOCKET_TYPE parse_socket_type_from_top_bits(u32 val)
{
	u32 top_bits = (val >> 28) & 0xF;

	if (top_bits == 0 || (top_bits & (top_bits - 1)) != 0) {
		return SOCKET_UNKNOWN;
	}

	if (top_bits == 0x8) return SOCKET_RESET;
	if (top_bits == 0x4) return SOCKET_HEARTBEAT;
	if (top_bits == 0x2) return SOCKET_ATTACH;
	if (top_bits == 0x1) return SOCKET_DETACH;

	return SOCKET_UNKNOWN;
}

static ssize_t socket_info_write(struct file *file,
				const char __user * user_buffer, size_t count,
				loff_t * offset)
{
	char kbuf[KBUF_LEN];
	char timestamp[TIMESTAMP_LEN];
	int ret;
	u32 val;

	if (count < KBUF_MIN_INPUT_LEN || count >= KBUF_LEN) {
		hmbird_pr_err("Invailid argument: count = %zu\n", count);
		return -EINVAL;
	}

	if (copy_from_user(kbuf, user_buffer, count)) {
		hmbird_pr_err("Copy from user failed\n");
		return -EFAULT;
	}

	kbuf[count] = '\0';

	ret = sscanf(kbuf, "%24s %d", timestamp, &val);
	if (ret != 2) {
		hmbird_pr_err("Failed to execute sscanf\n");
		return -EFAULT;
	}

	hmbird_pr_debug("writing socket_info: timestamp = %s, socket = %d\n", timestamp, val);
	strncpy(g_socket_info[g_socket_info_head].timestamp, timestamp,
		sizeof(g_socket_info[g_socket_info_head].timestamp) - 1);
	g_socket_info[g_socket_info_head].timestamp[sizeof(g_socket_info[g_socket_info_head].timestamp) - 1] = '\0';
	g_socket_info[g_socket_info_head].socket = val;

	g_socket_info_head = (g_socket_info_head + 1) % SOCKET_INFO_MAX_NUMBER;
	if (g_socket_info_head == 0) {
		g_socket_info_full = 1;
	}

	return count;
}

static int socket_info_show(struct seq_file *m, void *v)
{
	char *buf;
	char binary_str[40];
	int i, ret, start, cnt, socket_idx, buf_idx = 0;

	buf = (char*)kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = snprintf(&buf[buf_idx], (PAGE_SIZE - buf_idx), "%-25s%-14s%-39s\n",
			"time", "type", "socket (binary)");

	if ((ret < 0) || (ret >= PAGE_SIZE - buf_idx))
		goto err;
	buf_idx += ret;

	if (g_socket_info_full) {
		start = g_socket_info_head;
		cnt = SOCKET_INFO_MAX_NUMBER;
	} else {
		start = 0;
		cnt = g_socket_info_head;
	}

	for (i = 0; i < cnt; i++) {
		socket_idx = (start + i) % SOCKET_INFO_MAX_NUMBER;
		u32_to_bin_str(g_socket_info[socket_idx].socket, binary_str);
		enum SOCKET_TYPE type = parse_socket_type_from_top_bits(g_socket_info[socket_idx].socket);


		ret = snprintf(&buf[buf_idx], (PAGE_SIZE - buf_idx), "%-25s%-14s%-39s\n",
				g_socket_info[socket_idx].timestamp,
				socket_type_str[type],
				binary_str);

		if ((ret < 0) || (ret >= PAGE_SIZE - buf_idx))
			goto err;
		buf_idx += ret;
	}

	buf[buf_idx] = '\0';
	seq_printf(m, "%s\n", buf);
	kfree(buf);

	return 0;

err:
	kfree(buf);
	return -EFAULT;
}

static int socket_info_open(struct inode *inode, struct file *file)
{
	return single_open(file, socket_info_show, inode);
}

static const struct proc_ops socket_info_fops = {
	.proc_open	= socket_info_open,
	.proc_write	= socket_info_write,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
};

static ssize_t manager_info_write(struct file *file,
				const char __user * user_buffer, size_t count,
				loff_t * offset)
{
	char kbuf[KBUF_LEN];
	char timestamp[TIMESTAMP_LEN];
	char scene[SCENE_TYPE_MAX_LEN];
	char action[SCENE_STATUS_MAX_LEN];
	char package_name[ANDROID_PACKAGE_NAME_MAX_LEN];
	int ret;

	if (count < KBUF_MIN_INPUT_LEN || count >= KBUF_LEN) {
		hmbird_pr_err("Invailid argument: count = %zu\n", count);
		return -EINVAL;
	}

	if (copy_from_user(kbuf, user_buffer, count)) {
		hmbird_pr_err("Copy from user failed\n");
		return -EFAULT;
	}

	kbuf[count] = '\0';

	ret = sscanf(kbuf, "%24s %31s %11s %39s", timestamp, scene, action, package_name);
	if (ret != 4) {
		hmbird_pr_err("Failed to execute sscanf\n");
		return -EFAULT;
	}

	hmbird_pr_debug("writing manager_info: timestamp = %s, scene = %s, action = %s, package_name = %s\n", timestamp, scene, action, package_name);

	strncpy(g_manager_info[g_manager_info_head].timestamp, timestamp,
		sizeof(g_manager_info[g_manager_info_head].timestamp) - 1);
	g_manager_info[g_manager_info_head].timestamp[sizeof(g_manager_info[g_manager_info_head].timestamp) - 1] = '\0';

	strncpy(g_manager_info[g_manager_info_head].scene, scene,
		sizeof(g_manager_info[g_manager_info_head].scene) - 1);
	g_manager_info[g_manager_info_head].scene[sizeof(g_manager_info[g_manager_info_head].scene) - 1] = '\0';

	strncpy(g_manager_info[g_manager_info_head].action, action,
		sizeof(g_manager_info[g_manager_info_head].action) - 1);
	g_manager_info[g_manager_info_head].action[sizeof(g_manager_info[g_manager_info_head].action) - 1] = '\0';

	strncpy(g_manager_info[g_manager_info_head].package_name, package_name,
		sizeof(g_manager_info[g_manager_info_head].package_name) - 1);
	g_manager_info[g_manager_info_head].package_name[sizeof(g_manager_info[g_manager_info_head].package_name) - 1] = '\0';

	g_manager_info_head = (g_manager_info_head + 1) % MANAGER_INFO_MAX_NUMBER;
	if (g_manager_info_head == 0) {
		g_manager_info_full = 1;
	}

	return count;
}

static int manager_info_show(struct seq_file *m, void *v)
{
	char *buf;
	int i, ret, start, cnt, manager_idx, buf_idx = 0;

	buf = (char*)kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = snprintf(&buf[buf_idx], (PAGE_SIZE - buf_idx), "%-25s%-15s%-10s%-40s\n",
			"time", "scene", "action", "package name");

	if ((ret < 0) || (ret >= PAGE_SIZE - buf_idx))
		goto err;
	buf_idx += ret;

	if (g_manager_info_full) {
		start = g_manager_info_head;
		cnt = MANAGER_INFO_MAX_NUMBER;
	} else {
		start = 0;
		cnt = g_manager_info_head;
	}

	for (i = 0; i < cnt; i++) {
		manager_idx = (start + i) % MANAGER_INFO_MAX_NUMBER;

		ret = snprintf(&buf[buf_idx], (PAGE_SIZE - buf_idx), "%-25s%-15s%-10s%-40s\n",
				g_manager_info[manager_idx].timestamp,
				g_manager_info[manager_idx].scene,
				g_manager_info[manager_idx].action,
				g_manager_info[manager_idx].package_name);

		if ((ret < 0) || (ret >= PAGE_SIZE - buf_idx))
			goto err;
		buf_idx += ret;
	}

	buf[buf_idx] = '\0';
	seq_printf(m, "%s\n", buf);
	kfree(buf);

	return 0;

err:
	kfree(buf);
	return -EFAULT;
}

int manager_info_dump(char *buf, int buf_len)
{
	int i, ret, start, cnt, manager_idx, buf_idx = 0;

	if (!buf)
		return -ENOMEM;

	ret = snprintf(&buf[buf_idx], (buf_len - buf_idx), "%-25s%-15s%-10s%-40s\n",
			"time", "scene", "action", "package name");

	if ((ret < 0) || (ret >= buf_len - buf_idx))
		goto err;
	buf_idx += ret;

	if (g_manager_info_full) {
		start = g_manager_info_head;
		cnt = MANAGER_INFO_MAX_NUMBER;
	} else {
		start = 0;
		cnt = g_manager_info_head;
	}

	for (i = 0; i < cnt; i++) {
		manager_idx = (start + i) % MANAGER_INFO_MAX_NUMBER;

		ret = snprintf(&buf[buf_idx], (buf_len - buf_idx), "%-25s%-15s%-10s%-40s\n",
				g_manager_info[manager_idx].timestamp,
				g_manager_info[manager_idx].scene,
				g_manager_info[manager_idx].action,
				g_manager_info[manager_idx].package_name);

		if ((ret < 0) || (ret >= PAGE_SIZE - buf_idx))
			goto err;
		buf_idx += ret;
	}

	buf[buf_idx] = '\0';
	buf[buf_len - 1] = '\0';
	return 0;
err:
	return -EFAULT;
}

static int manager_info_open(struct inode *inode, struct file *file)
{
	return single_open(file, manager_info_show, inode);
}

static const struct proc_ops manager_info_fops = {
	.proc_open	= manager_info_open,
	.proc_write	= manager_info_write,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
};

static bool is_expired_locked(struct hb_ctx *hb, unsigned long now)
{
	if (hb->state != HB_RUNNING) return false;
	if (hb->start_jiffies == 0) return false;

	unsigned long delta_jiffies = nsecs_to_jiffies(hb->timeout_nsec);
	if (delta_jiffies == 0) delta_jiffies = 1;

	return time_after(now, hb->last_feed_jiffies + delta_jiffies);
}

static void send_sig_to_process_locked(pid_t pid)
{
	if (pid <= 0) return;

	struct pid *p = find_get_pid(pid);
	if (!p) {
		hmbird_pr_warn("pid %d not found (struct pid) when sending signal\n", pid);
		return;
	}

	struct task_struct *t = get_pid_task(p, PIDTYPE_PID);
	if (!t) {
		hmbird_pr_warn("pid %d has no task when sending signal\n", pid);
		put_pid(p);
		return;
	}

	int ret = send_sig_info(NOTIFY_SIGNAL, SEND_SIG_PRIV, t);
	if (ret == 0)
		hmbird_pr_info("sent signal %d to pid %d via send_sig_info(priv)\n", NOTIFY_SIGNAL, pid);
	else
		hmbird_pr_warn("send_sig_info(%d) to pid %d failed: %d\n", NOTIFY_SIGNAL, pid, ret);

	put_task_struct(t);
	put_pid(p);
}

void notify_gpa_exit_hmbird(void)
{
	mutex_lock(&g_gpa_hb->lock);
	send_sig_to_process_locked(g_gpa_hb->pid);
	mutex_unlock(&g_gpa_hb->lock);
}

void notify_manager_exit(void)
{
	mutex_lock(&g_manager_hb->lock);
	send_sig_to_process_locked(g_manager_hb->pid);
	mutex_unlock(&g_manager_hb->lock);
}

static void notify_timeout_locked(struct hb_ctx *c)
{
	if (c->app == HB_MANAGER) {
		hmbird_pr_info("heartbeat - oplusHmbirdBpfManager TIMEOUT, notifying gpa to exit\n");
		send_sig_to_process_locked(g_gpa_hb->pid);
	}
}

static void timeout_workfn(struct work_struct *work)
{
	struct hb_ctx *c = container_of(to_delayed_work(work), struct hb_ctx, timeout_work);
	unsigned long now = jiffies;
	bool expired = false;

	mutex_lock(&c->lock);
	expired = is_expired_locked(c, now);
	if (expired) {
		c->state = HB_TIMEOUT;
		c->last_timeout_jiffies = now;
		c->timeout_count++;
		notify_timeout_locked(c);
	} else {
		if (c->state == HB_RUNNING) {
			unsigned long timeout_jiffies = nsecs_to_jiffies(c->timeout_nsec);
			if (timeout_jiffies == 0) timeout_jiffies = 1;
			schedule_delayed_work(&c->timeout_work, timeout_jiffies);
		}
	}
	mutex_unlock(&c->lock);
}

static ssize_t manager_hb_feed_write(struct file *filp, const char __user *ubuf, size_t len, loff_t *ppos)
{
	char *buf;
	ssize_t ret = len;
	unsigned long now = jiffies;

	if (len == 0 || len > 64)
		return -EINVAL;

	buf = kzalloc(len + 1, GFP_KERNEL);
	if (!buf) { return -ENOMEM; }
	if (copy_from_user(buf, ubuf, len)) {
		kfree(buf);
		return -EFAULT;
	}
	buf[len] = '\0';

	if (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
		buf[len - 1] = '\0';

	mutex_lock(&g_manager_hb->lock);

	if (!strcmp(buf, "start")) {
		g_manager_hb->start_count++;
		if (g_manager_hb->state == HB_TIMEOUT) {
			g_manager_hb->timeout_restart_count++;
		}
		g_manager_hb->state = HB_RUNNING;
		g_manager_hb->start_jiffies = now;
		g_manager_hb->last_feed_jiffies = now;
		g_manager_hb->feed_count = 0;

		hmbird_pr_info("heartbeat - start, start_jiffies = %lu\n", g_manager_hb->start_jiffies);

		cancel_delayed_work(&g_manager_hb->timeout_work);
		unsigned long timeout_jiffies = nsecs_to_jiffies(g_manager_hb->timeout_nsec);
		if (timeout_jiffies == 0) timeout_jiffies = 1;
		schedule_delayed_work(&g_manager_hb->timeout_work, timeout_jiffies);
	} else if (!strcmp(buf, "feed")) {
		if (g_manager_hb->state == HB_RUNNING) {
			g_manager_hb->last_feed_jiffies = now;
			g_manager_hb->feed_count++;

			hmbird_pr_debug("heartbeat - feed, feed_count = %lu\n", g_manager_hb->feed_count);
		}
	} else if (!strcmp(buf, "stop")) {
		g_manager_hb->stop_count++;
		if (g_manager_hb->state == HB_IDLE) {
			g_manager_hb->last_stop_jiffies = now;
			goto out;
		}

		cancel_delayed_work(&g_manager_hb->timeout_work);
		g_manager_hb->state = HB_IDLE;
		g_manager_hb->last_stop_jiffies = now;

		hmbird_pr_info("heartbeat - stop, last_stop_jiffies = %lu\n", g_manager_hb->last_stop_jiffies);
	}

out:
	mutex_unlock(&g_manager_hb->lock);

	kfree(buf);
	return ret;
}

static const struct proc_ops manager_hb_feed_fops = {
	.proc_write = manager_hb_feed_write,
};

static int manager_hb_stat_show(struct seq_file *m, void *v)
{
	int state = READ_ONCE(g_manager_hb->state);
	u64 timeout_ns = READ_ONCE(g_manager_hb->timeout_nsec);
	unsigned long long timeout_sec = timeout_ns / NS_PER_SEC;
	unsigned long long timeout_msec = (timeout_ns % NS_PER_SEC) / 1000000ULL;

	seq_printf(m,
		"state: %s\n"
		"manager_pid: %u\n"
		"timeout_sec: %llu.%03llu s\n"
		"start_count: %lu\n"
		"stop_count: %lu\n"
		"timeout_count: %lu\n"
		"timeout_restart_count: %lu\n"
		"feed_count: %lu\n"
		"last_start_jiffies: %lu\n"
		"last_feed_jiffies: %lu\n"
		"last_timeout_jiffies: %lu\n",
		((state == HB_IDLE) ? "idle" : ((state == HB_RUNNING) ? "running" : "timeout")),
		READ_ONCE(g_manager_hb->pid),
		timeout_sec, timeout_msec,
		READ_ONCE(g_manager_hb->start_count),
		READ_ONCE(g_manager_hb->stop_count),
		READ_ONCE(g_manager_hb->timeout_count),
		READ_ONCE(g_manager_hb->timeout_restart_count),
		READ_ONCE(g_manager_hb->feed_count),
		READ_ONCE(g_manager_hb->start_jiffies),
		READ_ONCE(g_manager_hb->last_feed_jiffies),
		READ_ONCE(g_manager_hb->last_timeout_jiffies));

	return 0;
}

static int manager_hb_stat_open(struct inode *inode, struct file *file)
{
	return single_open(file, manager_hb_stat_show, NULL);
}

static const struct proc_ops manager_hb_stat_fops = {
	.proc_open    = manager_hb_stat_open,
	.proc_read    = seq_read,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
};

static ssize_t manager_pid_write(struct file *file, const char __user *ubuf, size_t len, loff_t *ppos)
{
	char buf[32] = {0};
	long pid = 0;

	if ((len == 0) || (len >= sizeof(buf))) {
		return -EINVAL;
	}
	if (copy_from_user(buf, ubuf, len)) {
		return -EFAULT;
	}
	buf[len] = '\0';

	if (kstrtol(buf, 10, &pid) != 0) {
		return -EINVAL;
	}

	mutex_lock(&g_manager_hb->lock);
	g_manager_hb->pid = (pid_t)pid;
	hmbird_pr_info("manager pid set to %d\n", g_manager_hb->pid);
	mutex_unlock(&g_manager_hb->lock);
	return len;
}

static ssize_t manager_pid_read(struct file *file, char __user *ubuf, size_t count, loff_t *ppos)
{
	char tmp[32];
	int n = scnprintf(tmp, sizeof(tmp), "%d\n", READ_ONCE(g_manager_hb->pid));
	return simple_read_from_buffer(ubuf, count, ppos, tmp, n);
}

static const struct proc_ops manager_pid_fops = {
	.proc_read  = manager_pid_read,
	.proc_write = manager_pid_write,
};

static ssize_t gpa_pid_write(struct file *file, const char __user *ubuf, size_t len, loff_t *ppos)
{
	char buf[32] = {0};
	long pid = 0;

	if ((len == 0) || (len >= sizeof(buf))) {
		return -EINVAL;
	}
	if (copy_from_user(buf, ubuf, len)) {
		return -EFAULT;
	}
	buf[len] = '\0';

	if (kstrtol(buf, 10, &pid) != 0) {
		return -EINVAL;
	}

	mutex_lock(&g_gpa_hb->lock);
	g_gpa_hb->pid = (pid_t)pid;
	hmbird_pr_info("gpa pid set to %d\n", g_gpa_hb->pid);
	mutex_unlock(&g_gpa_hb->lock);
	return len;
}

static ssize_t gpa_pid_read(struct file *file, char __user *ubuf, size_t count, loff_t *ppos)
{
	char tmp[32];
	int n = scnprintf(tmp, sizeof(tmp), "%d\n", READ_ONCE(g_gpa_hb->pid));
	return simple_read_from_buffer(ubuf, count, ppos, tmp, n);
}

static const struct proc_ops gpa_pid_fops = {
	.proc_read  = gpa_pid_read,
	.proc_write = gpa_pid_write,
};

static int summary_show(struct seq_file *m, void *v)
{
	int ret = 0;

	seq_printf(m, "[%s]\n", "compact_scene_stat");
	ret |= compact_scene_stat_show(m, v);

	seq_puts(m, "=========================================================================================\n");

	seq_printf(m, "[%s]\n", "scene_stat");
	ret |= scene_stat_show(m, v);

	seq_puts(m, "=========================================================================================\n");

	seq_printf(m, "[%s]\n", "socket_info");
	ret |= socket_info_show(m, v);

	seq_puts(m, "=========================================================================================\n");

	seq_printf(m, "[%s]\n", "manager_info");
	ret |= manager_info_show(m, v);

	seq_puts(m, "=========================================================================================\n");

	seq_printf(m, "[%s]\n", "manager_hb_stat");
	ret |= manager_hb_stat_show(m, v);

	return ret;
}

static int summary_open(struct inode *inode, struct file *file)
{
	return single_open(file, summary_show, inode);
}

static const struct proc_ops summary_fops = {
	.proc_open	= summary_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
};

struct proc_dir_entry *d_oplus_hmbird;

static int create_stats_procs(void) {
	struct proc_dir_entry *p;

	d_oplus_hmbird = proc_mkdir(OPLUS_HMBIRD_PROC_DIR, NULL);
	if (d_oplus_hmbird) {
		hmbird_pr_info("sysfs dir init success!!\n");
	} else {
		goto err;
	}

	p = proc_create(SCENE_STAT_PROC, S_IRUGO | S_IWUGO,
				d_oplus_hmbird, &scene_stat_fops);
	if (p == NULL) {
		hmbird_pr_debug("failed to proc_create %s\n", SCENE_STAT_PROC);
		goto err_scene_stat;
	}

	p = proc_create(COMPACT_SCENE_STAT_PROC, S_IRUGO | S_IWUGO,
			d_oplus_hmbird, &compact_scene_stat_fops);
	if (p == NULL) {
		hmbird_pr_debug("failed to proc_create %s\n", COMPACT_SCENE_STAT_PROC);
		goto err_compact_scene_stat;
	}

	p = proc_create(SOCKET_INFO_PROC, S_IRUGO | S_IWUGO,
			d_oplus_hmbird, &socket_info_fops);
	if (p == NULL) {
		hmbird_pr_debug("failed to proc_create %s\n", SOCKET_INFO_PROC);
		goto err_socket_info;
	}

	p = proc_create(MANAGER_INFO_PROC, S_IRUGO | S_IWUGO,
			d_oplus_hmbird, &manager_info_fops);
	if (p == NULL) {
		hmbird_pr_debug("failed to proc_create %s\n", MANAGER_INFO_PROC);
		goto err_manager_info;
	}

	p = proc_create(SUMMARY_PROC, S_IRUGO | S_IWUGO,
			d_oplus_hmbird, &summary_fops);
	if (p == NULL) {
		hmbird_pr_debug("failed to proc_create %s\n", SUMMARY_PROC);
		goto err_summary;
	}

	p = proc_create(MANAGER_HB_STAT_PROC, S_IRUGO | S_IWUGO,
			d_oplus_hmbird, &manager_hb_stat_fops);
	if (p == NULL) {
		hmbird_pr_debug("failed to proc_create %s\n", MANAGER_HB_STAT_PROC);
		goto err_manager_hb_stat;
	}

	p = proc_create(MANAGER_HB_FEED_PROC, S_IRUGO | S_IWUGO,
			d_oplus_hmbird, &manager_hb_feed_fops);
	if (p == NULL) {
		hmbird_pr_debug("failed to proc_create %s\n", MANAGER_HB_FEED_PROC);
		goto err_manager_hb_feed;
	}

	p = proc_create(MANAGER_PID_PROC, S_IRUGO | S_IWUGO,
			d_oplus_hmbird, &manager_pid_fops);
	if (p == NULL) {
		hmbird_pr_debug("failed to proc_create %s\n", MANAGER_PID_PROC);
		goto err_manager_pid;
	}

	p = proc_create(GPA_PID_PROC, S_IRUGO | S_IWUGO,
			d_oplus_hmbird, &gpa_pid_fops);
	if (p == NULL) {
		hmbird_pr_debug("failed to proc_create %s\n", GPA_PID_PROC);
		goto err_gpa_pid;
	}

	return 0;

err_gpa_pid:
	hmbird_pr_debug("doing remove_proc_entry %s\n", MANAGER_PID_PROC);
	remove_proc_entry(MANAGER_PID_PROC, d_oplus_hmbird);
err_manager_pid:
	hmbird_pr_debug("doing remove_proc_entry %s\n", MANAGER_HB_FEED_PROC);
	remove_proc_entry(MANAGER_HB_FEED_PROC, d_oplus_hmbird);
err_manager_hb_feed:
	hmbird_pr_debug("doing remove_proc_entry %s\n", MANAGER_HB_STAT_PROC);
	remove_proc_entry(MANAGER_HB_STAT_PROC, d_oplus_hmbird);
err_manager_hb_stat:
	hmbird_pr_debug("doing remove_proc_entry %s\n", SUMMARY_PROC);
	remove_proc_entry(SUMMARY_PROC, d_oplus_hmbird);
err_summary:
	hmbird_pr_debug("doing remove_proc_entry %s\n", MANAGER_INFO_PROC);
	remove_proc_entry(MANAGER_INFO_PROC, d_oplus_hmbird);
err_manager_info:
	hmbird_pr_debug("doing remove_proc_entry %s\n", SOCKET_INFO_PROC);
	remove_proc_entry(SOCKET_INFO_PROC, d_oplus_hmbird);
err_socket_info:
	hmbird_pr_debug("doing remove_proc_entry %s\n", COMPACT_SCENE_STAT_PROC);
	remove_proc_entry(COMPACT_SCENE_STAT_PROC, d_oplus_hmbird);
err_compact_scene_stat:
	hmbird_pr_debug("doing remove_proc_entry %s\n", SCENE_STAT_PROC);
	remove_proc_entry(SCENE_STAT_PROC, d_oplus_hmbird);
err_scene_stat:
	hmbird_pr_debug("doing remove_proc_entry %s\n", OPLUS_HMBIRD_PROC_DIR);
	remove_proc_entry(OPLUS_HMBIRD_PROC_DIR, d_oplus_hmbird);
err:
	return -ENOMEM;
}

static void remove_stats_procs(void)
{
	hmbird_pr_debug("doing remove_proc_entry %s\n", GPA_PID_PROC);
	remove_proc_entry(GPA_PID_PROC, d_oplus_hmbird);

	hmbird_pr_debug("doing remove_proc_entry %s\n", MANAGER_PID_PROC);
	remove_proc_entry(MANAGER_PID_PROC, d_oplus_hmbird);

	hmbird_pr_debug("doing remove_proc_entry %s\n", MANAGER_HB_FEED_PROC);
	remove_proc_entry(MANAGER_HB_FEED_PROC, d_oplus_hmbird);

	hmbird_pr_debug("doing remove_proc_entry %s\n", MANAGER_HB_STAT_PROC);
	remove_proc_entry(MANAGER_HB_STAT_PROC, d_oplus_hmbird);

	hmbird_pr_debug("doing remove_proc_entry %s\n", SUMMARY_PROC);
	remove_proc_entry(SUMMARY_PROC, d_oplus_hmbird);

	hmbird_pr_debug("doing remove_proc_entry %s\n", MANAGER_INFO_PROC);
	remove_proc_entry(MANAGER_INFO_PROC, d_oplus_hmbird);

	hmbird_pr_debug("doing remove_proc_entry %s\n", SOCKET_INFO_PROC);
	remove_proc_entry(SOCKET_INFO_PROC, d_oplus_hmbird);

	hmbird_pr_debug("doing remove_proc_entry %s\n", COMPACT_SCENE_STAT_PROC);
	remove_proc_entry(COMPACT_SCENE_STAT_PROC, d_oplus_hmbird);

	hmbird_pr_debug("doing remove_proc_entry %s\n", SCENE_STAT_PROC);
	remove_proc_entry(SCENE_STAT_PROC, d_oplus_hmbird);
}

int manager_hb_init(void)
{
	g_manager_hb = kzalloc(sizeof(*g_manager_hb), GFP_KERNEL);
	if (!g_manager_hb) return -ENOMEM;

	g_gpa_hb = kzalloc(sizeof(*g_gpa_hb), GFP_KERNEL);
	if (!g_gpa_hb) return -ENOMEM;

	mutex_init(&g_manager_hb->lock);
	g_manager_hb->app = HB_MANAGER;
	g_manager_hb->state = HB_IDLE;
	g_manager_hb->timeout_nsec = MANAGER_HB_TIMEOUT_NSEC;

	INIT_DELAYED_WORK(&g_manager_hb->timeout_work, timeout_workfn);

	g_manager_hb->start_count = 0;
	g_manager_hb->stop_count = 0;
	g_manager_hb->timeout_restart_count = 0;
	g_manager_hb->timeout_count = 0;
	g_manager_hb->feed_count = 0;
	g_manager_hb->pid = 0;

	mutex_init(&g_gpa_hb->lock);
	g_gpa_hb->pid = 0;

	hmbird_pr_info("loaded manager_hb, timeout = %llu ns\n", g_manager_hb->timeout_nsec);
	return 0;
}

void manager_hb_exit(void)
{
	if (!g_manager_hb) return;

	cancel_delayed_work_sync(&g_manager_hb->timeout_work);
	mutex_destroy(&g_manager_hb->lock);
	kfree(g_manager_hb);
	g_manager_hb = NULL;

	hmbird_pr_info("unloaded manager_hb.\n");
}

int hmbird_dfx_init(void)
{
	int ret;

	ret = create_stats_procs();
	if (ret < 0) {
		goto err;
	}

	ret = manager_hb_init();
	if (ret < 0) {
		goto err;
	}

	return 0;
err:
	return ret;
}

void hmbird_dfx_exit(void)
{
	remove_stats_procs();
	manager_hb_exit();
}
