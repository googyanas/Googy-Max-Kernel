/*
 * Copyright (C) 2011-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_GROUP_H__
#define __MALI_GROUP_H__

#include "linux/jiffies.h"
#include "mali_osk.h"
#include "mali_l2_cache.h"
#include "mali_mmu.h"
#include "mali_gp.h"
#include "mali_pp.h"
#include "mali_session.h"

/* max runtime [ms] for a core job - used by timeout timers  */
#define MAX_RUNTIME 5000
/** @brief A mali group object represents a MMU and a PP and/or a GP core.
 *
 */
#define MALI_MAX_NUMBER_OF_GROUPS 10

enum maliggy_group_core_state
{
	MALI_GROUP_STATE_IDLE,
	MALI_GROUP_STATE_WORKING,
	MALI_GROUP_STATE_OOM,
	MALI_GROUP_STATE_IN_VIRTUAL,
	MALI_GROUP_STATE_JOINING_VIRTUAL,
	MALI_GROUP_STATE_LEAVING_VIRTUAL,
	MALI_GROUP_STATE_DISABLED,
};

/* Forward declaration from maliggy_pm_domain.h */
struct maliggy_pm_domain;

/**
 * The structure represents a render group
 * A render group is defined by all the cores that share the same Mali MMU
 */

struct maliggy_group
{
	struct maliggy_mmu_core        *mmu;
	struct maliggy_session_data    *session;
	int                         page_dir_ref_count;

	maliggy_bool                   power_is_on;
	enum maliggy_group_core_state  state;

	struct maliggy_gp_core         *gp_core;
	struct maliggy_gp_job          *gp_running_job;

	struct maliggy_pp_core         *pp_core;
	struct maliggy_pp_job          *pp_running_job;
	u32                         pp_running_sub_job;

	struct maliggy_l2_cache_core   *l2_cache_core[2];
	u32                         l2_cache_core_ref_count[2];

	struct maliggy_dlbu_core       *dlbu_core;
	struct maliggy_bcast_unit      *bcast_core;

	_maliggy_osk_lock_t            *lock;

	_maliggy_osk_list_t            pp_scheduler_list;

	/* List used for virtual groups. For a virtual group, the list represents the
	 * head element. */
	_maliggy_osk_list_t            group_list;

	struct maliggy_group           *pm_domain_list;
	struct maliggy_pm_domain       *pm_domain;

	/* Parent virtual group (if any) */
	struct maliggy_group           *parent_group;

	_maliggy_osk_wq_work_t         *bottom_half_work_mmu;
	_maliggy_osk_wq_work_t         *bottom_half_work_gp;
	_maliggy_osk_wq_work_t         *bottom_half_work_pp;

	_maliggy_osk_timer_t           *timeout_timer;
	maliggy_bool                   core_timed_out;
};

/** @brief Create a new Mali group object
 *
 * @param cluster Pointer to the cluster to which the group is connected.
 * @param mmu Pointer to the MMU that defines this group
 * @return A pointer to a new group object
 */
struct maliggy_group *maliggy_group_create(struct maliggy_l2_cache_core *core,
                                     struct maliggy_dlbu_core *dlbu,
				     struct maliggy_bcast_unit *bcast);

_maliggy_osk_errcode_t maliggy_group_add_mmu_core(struct maliggy_group *group, struct maliggy_mmu_core* mmu_core);
void maliggy_group_remove_mmu_core(struct maliggy_group *group);

_maliggy_osk_errcode_t maliggy_group_add_gp_core(struct maliggy_group *group, struct maliggy_gp_core* gp_core);
void maliggy_group_remove_gp_core(struct maliggy_group *group);

_maliggy_osk_errcode_t maliggy_group_add_pp_core(struct maliggy_group *group, struct maliggy_pp_core* pp_core);
void maliggy_group_remove_pp_core(struct maliggy_group *group);

void maliggy_group_set_pm_domain(struct maliggy_group *group, struct maliggy_pm_domain *domain);

void maliggy_group_delete(struct maliggy_group *group);

/** @brief Virtual groups */
void maliggy_group_add_group(struct maliggy_group *parent, struct maliggy_group *child, maliggy_bool update_hw);
void maliggy_group_remove_group(struct maliggy_group *parent, struct maliggy_group *child);
struct maliggy_group *maliggy_group_acquire_group(struct maliggy_group *parent);

MALI_STATIC_INLINE maliggy_bool maliggy_group_is_virtual(struct maliggy_group *group)
{
	return (NULL != group->dlbu_core);
}

/** @brief Check if a group is considered as part of a virtual group
 *
 * @note A group is considered to be "part of" a virtual group also during the transition
 *       in to / out of the virtual group.
 */
MALI_STATIC_INLINE maliggy_bool maliggy_group_is_in_virtual(struct maliggy_group *group)
{
	return (MALI_GROUP_STATE_IN_VIRTUAL == group->state ||
	        MALI_GROUP_STATE_JOINING_VIRTUAL == group->state ||
	        MALI_GROUP_STATE_LEAVING_VIRTUAL == group->state);
}

/** @brief Reset group
 *
 * This function will reset the entire group, including all the cores present in the group.
 *
 * @param group Pointer to the group to reset
 */
void maliggy_group_reset(struct maliggy_group *group);

/** @brief Zap MMU TLB on all groups
 *
 * Zap TLB on group if \a session is active.
 */
void maliggy_group_zap_session(struct maliggy_group* group, struct maliggy_session_data *session);

/** @brief Get pointer to GP core object
 */
struct maliggy_gp_core* maliggy_group_get_gp_core(struct maliggy_group *group);

/** @brief Get pointer to PP core object
 */
struct maliggy_pp_core* maliggy_group_get_pp_core(struct maliggy_group *group);

/** @brief Lock group object
 *
 * Most group functions will lock the group object themselves. The expection is
 * the group_bottom_half which requires the group to be locked on entry.
 *
 * @param group Pointer to group to lock
 */
void maliggy_group_lock(struct maliggy_group *group);

/** @brief Unlock group object
 *
 * @param group Pointer to group to unlock
 */
void maliggy_group_unlock(struct maliggy_group *group);
#ifdef DEBUG
void maliggy_group_assert_locked(struct maliggy_group *group);
#define MALI_ASSERT_GROUP_LOCKED(group) maliggy_group_assert_locked(group)
#else
#define MALI_ASSERT_GROUP_LOCKED(group)
#endif

/** @brief Start GP job
 */
void maliggy_group_start_gp_job(struct maliggy_group *group, struct maliggy_gp_job *job);
/** @brief Start fragment of PP job
 */
void maliggy_group_start_pp_job(struct maliggy_group *group, struct maliggy_pp_job *job, u32 sub_job);

/** @brief Resume GP job that suspended waiting for more heap memory
 */
struct maliggy_gp_job *maliggy_group_resume_gp_with_new_heap(struct maliggy_group *group, u32 job_id, u32 start_addr, u32 end_addr);
/** @brief Abort GP job
 *
 * Used to abort suspended OOM jobs when user space failed to allocte more memory.
 */
void maliggy_group_abort_gp_job(struct maliggy_group *group, u32 job_id);
/** @brief Abort all GP jobs from \a session
 *
 * Used on session close when terminating all running and queued jobs from \a session.
 */
void maliggy_group_abort_session(struct maliggy_group *group, struct maliggy_session_data *session);

maliggy_bool maliggy_group_power_is_on(struct maliggy_group *group);
void maliggy_group_power_on_group(struct maliggy_group *group);
void maliggy_group_power_off_group(struct maliggy_group *group);
void maliggy_group_power_on(void);
void maliggy_group_power_off(void);

struct maliggy_group *maliggy_group_get_glob_group(u32 index);
u32 maliggy_group_get_glob_num_groups(void);

u32 maliggy_group_dumpggy_state(struct maliggy_group *group, char *buf, u32 size);

/* MMU-related functions */
_maliggy_osk_errcode_t maliggy_group_upper_half_mmu(void * data);

/* GP-related functions */
_maliggy_osk_errcode_t maliggy_group_upper_half_gp(void *data);

/* PP-related functions */
_maliggy_osk_errcode_t maliggy_group_upper_half_pp(void *data);

/** @brief Check if group is enabled
 *
 * @param group group to check
 * @return MALI_TRUE if enabled, MALI_FALSE if not
 */
maliggy_bool maliggy_group_is_enabled(struct maliggy_group *group);

/** @brief Enable group
 *
 * An enabled job is put on the idle scheduler list and can be used to handle jobs.  Does nothing if
 * group is already enabled.
 *
 * @param group group to enable
 */
void maliggy_group_enable(struct maliggy_group *group);

/** @brief Disable group
 *
 * A disabled group will no longer be used by the scheduler.  If part of a virtual group, the group
 * will be removed before being disabled.  Cores part of a disabled group is safe to power down.
 *
 * @param group group to disable
 */
void maliggy_group_disable(struct maliggy_group *group);

MALI_STATIC_INLINE maliggy_bool maliggy_group_virtual_disable_if_empty(struct maliggy_group *group)
{
	maliggy_bool empty = MALI_FALSE;

	MALI_ASSERT_GROUP_LOCKED(group);
	MALI_DEBUG_ASSERT(maliggy_group_is_virtual(group));

	if (_maliggy_osk_list_empty(&group->group_list))
	{
		group->state = MALI_GROUP_STATE_DISABLED;
		group->session = NULL;

		empty = MALI_TRUE;
	}

	return empty;
}

MALI_STATIC_INLINE maliggy_bool maliggy_group_virtual_enable_if_empty(struct maliggy_group *group)
{
	maliggy_bool empty = MALI_FALSE;

	MALI_ASSERT_GROUP_LOCKED(group);
	MALI_DEBUG_ASSERT(maliggy_group_is_virtual(group));

	if (_maliggy_osk_list_empty(&group->group_list))
	{
		MALI_DEBUG_ASSERT(MALI_GROUP_STATE_DISABLED == group->state);

		group->state = MALI_GROUP_STATE_IDLE;

		empty = MALI_TRUE;
	}

	return empty;
}

#endif /* __MALI_GROUP_H__ */
