#include <linux/sched.h>
#include <linux/notifier.h>
#include <linux/panic_notifier.h>
#include <linux/sched/clock.h>
#include <linux/kdebug.h>
#include <../kernel/sched/sched.h>
#include "hmbird_minidump.h"
#include "hmbird_II/hmbird_II.h"
#include "hmbird_II/hmbird_II_shadow_tick.h"
#include "hmbird_dfx.h"
#include "trace_hmbird_common.h"

#ifdef CONFIG_ARCH_QCOM
#if IS_ENABLED(CONFIG_QCOM_MINIDUMP)
#include <soc/qcom/minidump.h>
#endif
#endif /* CONFIG_ARCH_QCOM */

#if 0
#ifdef CONFIG_ARCH_MEDIATEK
#if IS_ENABLED(CONFIG_MTK_AEE_IPANIC)
#include "drivers/misc/mediatek/aee/mrdump/mrdump_mini.h"
#endif
#endif /* CONFIG_ARCH_MEDIATEK */
#endif

struct ko_snap_misc_t {
	/* sw state */
	u64 scx_enabled;
	u64 scx_ops_enable_state_var;
	u64 hmbird_scene_enable;
	u64 curr_ss;
	/* sysctl value */
	u64 hmbird_debug;
	u64 highres_tick_ctrl;
	u64 highres_tick_ctrl_dbg;
	u64 cpus_reserved;
	u64 cpus_exclusive;
	u64 frame_per_sec;
	/* time record */
	u64 snap_jiffies;
	u64 snap_time;
};

#define KO_SNAP_MISC_ITEMS	(sizeof(struct ko_snap_misc_t) / sizeof(u64))
struct ko_panic_snapshot_t {
	struct meta_desc_t rq_nr_meta;
	u64 rq_nr[NR_CPUS];

	struct meta_desc_t scxrq_nr_meta;
	u64 scxrq_nr[NR_CPUS];

	struct meta_desc_t coef_meta;
	u64 coefficient[MAX_NR_CLUSTER];

	struct meta_desc_t high_ratio_meta;
	u64 perf_high_ratio[MAX_NR_CLUSTER];

	struct meta_desc_t freq_policy_meta;
	u64 freq_policy[MAX_NR_CLUSTER];

	struct meta_desc_t scene_stat_meta;
	struct scene_stats_snap scene_stat_sp;

	struct meta_desc_t hbmgr_info_meta;
	char hbmgr_info[DFL_MGR_DUMP_LEN];

	struct meta_desc_t snap_misc_meta;
	struct ko_snap_misc_t snap_misc;
};

struct bpf_snap_misc_t {
	u64 nr_dsqs;
	u64 nr_available_cluster;
	/* time record */
	u64 snap_jiffies;
	u64 snap_time;
};

struct dsq_status_t {
	u64 easy_dsq_id;
	u64 runnable_at;
	u64 timeout;
};
#define DSQ_STAT_ITEMS	(sizeof(struct dsq_status_t) / sizeof(u64))

#define BPF_SNAP_MISC_ITEMS	(sizeof(struct bpf_snap_misc_t) / sizeof(u64))
struct bpf_panic_snapshot_t {
	struct meta_desc_t dsq_stat_meta;
	struct dsq_status_t dsq_stat[MAX_NR_DSQS_MD];

	struct meta_desc_t snap_misc_meta;
	struct bpf_snap_misc_t snap_misc;
};

struct ko_info_t {
	struct meta_desc_t sw_rec_meta;
	struct hmbird_switch_t sw_rec[MAX_SWITCHS];

	struct meta_desc_t sw_idx_meta;
	u64 sw_idx;

	struct meta_desc_t excep_rec_meta;
	u64 excep_rec[MAX_KO_EXCEP_ID][MAX_EXCEPS];

	struct meta_desc_t excep_idx_meta;
	u64 excep_idx[MAX_KO_EXCEP_ID];

	/* snapshot while panic. */
	struct ko_panic_snapshot_t snap;
};

struct bpf_info_t {
	struct meta_desc_t excep_rec_meta;
	u64 excep_rec[MAX_BPF_EXCEP_ID][MAX_EXCEPS];

	struct meta_desc_t excep_idx_meta;
	u64 excep_idx[MAX_BPF_EXCEP_ID];

	struct bpf_panic_snapshot_t snap;
};

struct md_info_t {
	struct md_meta_t meta;
	struct ko_info_t ko_dump;
	struct bpf_info_t bpf_dump;
};


struct md_info_t *md_info;
enum hmbird_switch_reason_type sw_reason;
enum switch_end_stat curr_ses;

void sw_update(u64 is_success, u64 end_state, u64 switch_reason)
{
	u64 *idx;

	if (!md_info)
		return;

	if (end_state == HMBIRD_DISABLED || end_state == HMBIRD_ENABLED) {
		idx = &md_info->ko_dump.sw_idx;
		md_info->ko_dump.sw_rec[*idx].switch_at = jiffies;
		md_info->ko_dump.sw_rec[*idx].is_success = is_success;
		md_info->ko_dump.sw_rec[*idx].end_state = end_state;
		md_info->ko_dump.sw_rec[*idx].switch_reason = switch_reason;
		*idx = ++(*idx) % MAX_SWITCHS;
	}

	curr_ses = end_state;
}

inline void ko_exceps_update(int id, unsigned long jiffies)
{
	u64 *idx;

	if (!md_info || id > MAX_KO_EXCEP_ID)
		return;

	idx = &md_info->ko_dump.excep_idx[id];
	md_info->ko_dump.excep_rec[id][*idx] = jiffies;
	*idx = ++(*idx) % MAX_EXCEPS;
}

inline void bpf_exceps_update(int id, unsigned long jiffies)
{
	u64 *idx;

	if (!md_info || id > MAX_BPF_EXCEP_ID)
		return;

	idx = &md_info->bpf_dump.excep_idx[id];
	md_info->bpf_dump.excep_rec[id][*idx] = jiffies;
	*idx = ++(*idx) % MAX_EXCEPS;
}

extern void get_scx_state(int *ops_enabled, int *ops_enable_state_var);
extern int scene_stats_dump(struct scene_stats_snap *m_scene_stats_snap);
extern int manager_info_dump(char *buf, int buf_len);
static void get_ko_hmbird_snapshot(struct ko_panic_snapshot_t *pnc)
{
	struct rq *rq;
	int cpu, cluster_id;
	int ops_enable, scx_ops_enable_state;
	cpumask_t cpus_exclusive, cpus_reserved;

	for_each_possible_cpu(cpu) {
		rq = cpu_rq(cpu);
		pnc->rq_nr[cpu] = rq->nr_running;
		pnc->scxrq_nr[cpu] = rq->scx.nr_running;
	}

	for (cluster_id = 0; cluster_id < nr_cluster; cluster_id++) {
		pnc->coefficient[cluster_id] = cfg_coefficient_get(cluster_id);
		pnc->perf_high_ratio[cluster_id] = cfg_perf_high_ratio_get(cluster_id);
		pnc->freq_policy[cluster_id] = cfg_freq_policy_get(cluster_id);
	}
	get_scx_state(&ops_enable, &scx_ops_enable_state);
	pnc->snap_misc.scx_enabled = ops_enable;
	pnc->snap_misc.scx_ops_enable_state_var = scx_ops_enable_state;
	pnc->snap_misc.hmbird_scene_enable = hmbird_enable;
	pnc->snap_misc.curr_ss = curr_ses;
	pnc->snap_misc.hmbird_debug = hmbird_debug;
	pnc->snap_misc.highres_tick_ctrl = highres_tick_ctrl;
	pnc->snap_misc.highres_tick_ctrl_dbg = highres_tick_ctrl_dbg;
	cfg_cpus_get(&cpus_exclusive, &cpus_reserved);
	pnc->snap_misc.cpus_exclusive = cpus_exclusive.bits[0];
	pnc->snap_misc.cpus_reserved = cpus_reserved.bits[0];
	pnc->snap_misc.frame_per_sec = cfg_frame_per_sec_get();
	pnc->snap_misc.snap_jiffies = jiffies;
	pnc->snap_misc.snap_time = local_clock();
	scene_stats_dump(&pnc->scene_stat_sp);
	manager_info_dump(pnc->hbmgr_info, DFL_MGR_DUMP_LEN);
}

static int dsq_snap_cnt = 0;
inline void bpf_snapshot_misc_update(int nr_dsq, int nr_aval_clus)
{
	struct bpf_panic_snapshot_t *snap = &md_info->bpf_dump.snap;

	if (nr_dsq > MAX_NR_DSQS_MD) {
		nr_dsq = MAX_NR_DSQS_MD;
	}
	dsq_snap_cnt = 0;
	snap->snap_misc.nr_dsqs = (u64)nr_dsq;
	snap->snap_misc.nr_available_cluster = (u64)nr_aval_clus;
	snap->snap_misc.snap_jiffies = jiffies;
	snap->snap_misc.snap_time = local_clock();
}

inline void bpf_snapshot_dsq_update(u64 easy_dsq_id, u64 runnable_at, u64 dsq_timeout)
{
	struct bpf_panic_snapshot_t *snap = &md_info->bpf_dump.snap;

	if (dsq_snap_cnt >= MAX_NR_DSQS_MD || dsq_snap_cnt < 0) {
		return;
	}
	snap->dsq_stat[dsq_snap_cnt].easy_dsq_id = easy_dsq_id;
	snap->dsq_stat[dsq_snap_cnt].runnable_at = runnable_at;
	snap->dsq_stat[dsq_snap_cnt].timeout = dsq_timeout;
	dsq_snap_cnt ++;
}

static void init_desc_meta(struct meta_desc_t *m, char *desc_str,
		u64 unit_type, u64 d1, u64 d2, u64 d3)
{
	strscpy(m->desc_str, desc_str, DESC_STR_LEN);
	m->unit_type = unit_type;
	m->desc_len_u64 = d1 * d2 * d3;
	m->each_dimen_len[0] = d1;
	m->each_dimen_len[1] = d2;
	m->each_dimen_len[2] = d3;
}

static void init_desc_metas(struct md_info_t *m)
{
	/* ko sw desc_metas */
	init_desc_meta(&m->ko_dump.sw_rec_meta, "switch_record",
			U64_UNIT_TYPE, 1, MAX_SWITCHS, SWITCH_ITEMS);
	init_desc_meta(&m->ko_dump.sw_idx_meta, "switch_idx",
			U64_UNIT_TYPE, 1, 1, 1);
	/* ko exceps desc_metas */
	init_desc_meta(&m->ko_dump.excep_rec_meta, "ko_excep_rec",
			U64_UNIT_TYPE, 1, MAX_KO_EXCEP_ID, MAX_EXCEPS);
	init_desc_meta(&m->ko_dump.excep_idx_meta, "ko_excep_idx",
			U64_UNIT_TYPE, 1, 1, MAX_KO_EXCEP_ID);
	/* ko snapshot desc_metas */
	init_desc_meta(&m->ko_dump.snap.rq_nr_meta, "rq_nr",
			U64_UNIT_TYPE, 1, 1, NR_CPUS);
	init_desc_meta(&m->ko_dump.snap.scxrq_nr_meta, "scxrq_nr",
			U64_UNIT_TYPE, 1, 1, NR_CPUS);
	init_desc_meta(&m->ko_dump.snap.coef_meta, "coefficient",
			U64_UNIT_TYPE, 1, 1, MAX_NR_CLUSTER);
	init_desc_meta(&m->ko_dump.snap.high_ratio_meta, "perf_high_ratio",
			U64_UNIT_TYPE, 1, 1, MAX_NR_CLUSTER);
	init_desc_meta(&m->ko_dump.snap.freq_policy_meta, "freq_policy",
			U64_UNIT_TYPE, 1, 1, MAX_NR_CLUSTER);
	init_desc_meta(&m->ko_dump.snap.scene_stat_meta, "scene_stats",
			U64_UNIT_TYPE, 1, SCENE_TYPES_COUNT, SCENE_STATUS_COUNT);
	init_desc_meta(&m->ko_dump.snap.hbmgr_info_meta, "manager_info",
			CHAR_UNIT_TYPE, 1, 1, DFL_MGR_DUMP_LEN_U64);
	init_desc_meta(&m->ko_dump.snap.snap_misc_meta, "ko_snap_misc",
			U64_UNIT_TYPE, 1, 1, KO_SNAP_MISC_ITEMS);
	/* bpf exceps desc_metas */
	init_desc_meta(&m->bpf_dump.excep_rec_meta, "excep_rec",
			U64_UNIT_TYPE, 1, MAX_BPF_EXCEP_ID, MAX_EXCEPS);
	init_desc_meta(&m->bpf_dump.excep_idx_meta, "excep_idx",
			U64_UNIT_TYPE, 1, 1, MAX_BPF_EXCEP_ID);
	/* bpf snapshot desc_metas*/
	init_desc_meta(&m->bpf_dump.snap.dsq_stat_meta, "dsq_stat",
			U64_UNIT_TYPE, 1, MAX_NR_DSQS_MD, DSQ_STAT_ITEMS);
	init_desc_meta(&m->bpf_dump.snap.snap_misc_meta, "bpf_snap_misc",
			U64_UNIT_TYPE, 1, 1, BPF_SNAP_MISC_ITEMS);
}

#define NR_META_DESC 11
static void init_md_meta(struct md_info_t *m)
{
	m->meta.self_md_meta_size = sizeof(struct md_meta_t);
	m->meta.dump_real_size = sizeof(struct md_info_t);
	m->meta.desc_meta_size = sizeof(struct meta_desc_t);
	m->meta.desc_str_size = DESC_STR_LEN;
	m->meta.desc_parse_dimens = PARSE_DIMENS;
	m->meta.desc_meta_nr = NR_META_DESC;
	m->meta.switches = MAX_SWITCHS;
	m->meta.exceps = MAX_EXCEPS;
	m->meta.nr_cpus = NR_CPUS;
	m->meta.real_cpus = nr_cpu_ids;

	init_desc_metas(m);
}

#define MINIDUMP_DFL_SIZE	(4 * 1024)

struct notifier_block hmbird_panic_blk;
struct notifier_block hmbird_die_blk;
static int hmbird_panic_handler(struct notifier_block *this,
					unsigned long event, void *ptr)
{
	if (!md_info)
		return NOTIFY_DONE;
	HMBIRD_ERR("hmbird_II minidump addr_px=0x%px, dump_size=%lu\n",
			md_info, (unsigned long)sizeof(struct md_info_t));
	trace_hmbird_minidump_snapshot(1);
	get_ko_hmbird_snapshot(&md_info->ko_dump.snap);

	return NOTIFY_DONE;
}

static int panic_blk_init(void)
{
	int ret = 0;
	int dump_size = max_t(u32, sizeof(struct md_info_t), MINIDUMP_DFL_SIZE);

	md_info = kzalloc(dump_size, GFP_KERNEL);
	if (!md_info) {
		HMBIRD_ERR("minidump alloc mem failed.\n");
		ret = -1;
		return ret;
	}
	init_md_meta(md_info);
	hmbird_panic_blk.notifier_call = hmbird_panic_handler;
	hmbird_panic_blk.priority = INT_MAX;
	ret = atomic_notifier_chain_register(&panic_notifier_list,
		&hmbird_panic_blk);
	if (ret) {
		HMBIRD_ERR	("register hmbird_panic_blk error, ret=%d\n", ret);
		return ret;
	}
	hmbird_die_blk.notifier_call = hmbird_panic_handler;
	hmbird_die_blk.priority = INT_MAX;
	ret = register_die_notifier(&hmbird_die_blk);
	if (ret) {
		HMBIRD_ERR("register hmbird_die_blk error, ret=%d\n", ret);
		return ret;
	}
	return 0;
}


#ifdef CONFIG_ARCH_QCOM
#if IS_ENABLED(CONFIG_QCOM_MINIDUMP)
extern int msm_minidump_add_region(const struct md_region *entry);

static int hmbird_md_register_qcom(void)
{
	unsigned long vaddr = (unsigned long)md_info;
	unsigned long size = sizeof(struct md_info_t);
	struct md_region hmbird_entry;

	if (!vaddr) {
		WARN_ON(1);
		return -1;
	}
	strncpy(hmbird_entry.name, "HMBIRD_II", sizeof(hmbird_entry.name));
	hmbird_entry.virt_addr = (uintptr_t)vaddr;
	hmbird_entry.phys_addr = virt_to_phys((unsigned long*)vaddr);
	hmbird_entry.size = size;
	if (msm_minidump_add_region(&hmbird_entry) < 0) {
		WARN_ON(1);
		return -1;
	}

	pr_info("hmbird_II QCOM minidump register success\n");
	return 0;
}
#endif /* CONFIG_QCOM_MINIDUMP */
#endif /* CONFIG_ARCH_QCOM */

#ifdef CONFIG_ARCH_MEDIATEK
#if IS_ENABLED(CONFIG_MTK_AEE_IPANIC)
extern void oplus_mrdump_mini_add_misc(unsigned long addr, unsigned long size,
		unsigned long start, char *name);

static int hmbird_md_register_mtk(void)
{
	unsigned long vaddr = (unsigned long)md_info;
	unsigned long size = sizeof(struct md_info_t);

	if (!vaddr) {
		WARN_ON(1);
		return -1;
	}

	oplus_mrdump_mini_add_misc(vaddr, size, 0, "load");
	pr_info("hmbird_II MTK minidump register success\n");
	return 0;
}
#endif /* CONFIG_MTK_AEE_IPANIC */
#endif /* CONFIG_ARCH_MEDIATEK */

static void hmbird_minidump_cleanup(void)
{
    if (md_info) {
        kfree(md_info);
        md_info = NULL;
    }
}

void hmbird_minidump_init(void)
{
	int ret = 0;

	ret = panic_blk_init();
	if (ret)
		goto err;

#if defined(CONFIG_ARCH_QCOM) && IS_ENABLED(CONFIG_QCOM_MINIDUMP)
	ret = hmbird_md_register_qcom();
	if (ret)
		goto err;
#elif defined(CONFIG_ARCH_MEDIATEK) && IS_ENABLED(CONFIG_MTK_AEE_IPANIC)
	ret = hmbird_md_register_mtk();
	if (ret)
		goto err;
#else
	HMBIRD_ERR("hmbird_II minidump not supported on this platform\n");
	ret = -1;
	goto err;
#endif
	pr_info("hmbird_II minidump initialized.\n");
	return;
err:
	hmbird_minidump_cleanup();
	HMBIRD_ERR("minidump initialization failed (%d)\n", ret);
}

void hmbird_minidump_exit(void) {
	hmbird_minidump_cleanup();
}

