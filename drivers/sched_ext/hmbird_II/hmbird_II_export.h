#ifndef _HMBIRD_II_EXPORT_H_
#define _HMBIRD_II_EXPORT_H_
#include "../../kernel/sched/sched.h"

#define SCX_RQ_BYPASS_HOOK		(1 << 18)

static inline bool hmbird_bypass_hooks(void)
{
	return cpu_rq(0)->scx.flags & SCX_RQ_BYPASS_HOOK;
}

static inline unsigned int hmbird_flt_get_mode(void)
{
	return 0;
}

#endif /*_HMBIRD_II_EXPORT_H_*/