/*
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_KERNEL_L2_CACHE_H__
#define __MALI_KERNEL_L2_CACHE_H__

#include "mali_osk.h"
#include "mali_hw_core.h"
#include "mali_pm_domain.h"

#define MALI_MAX_NUMBER_OF_L2_CACHE_CORES  3
/* Maximum 1 GP and 4 PP for an L2 cache core (Mali-400 Quad-core) */
#define MALI_MAX_NUMBER_OF_GROUPS_PER_L2_CACHE 5

struct maliggy_group;

/**
 * Definition of the L2 cache core struct
 * Used to track a L2 cache unit in the system.
 * Contains information about the mapping of the registers
 */
struct maliggy_l2_cache_core
{
	struct maliggy_hw_core  hw_core;      /**< Common for all HW cores */
	u32                  core_id;      /**< Unique core ID */
	_maliggy_osk_lock_t    *command_lock; /**< Serialize all L2 cache commands */
	_maliggy_osk_lock_t    *counter_lock; /**< Synchronize L2 cache counter access */
	u32                  counter_src0; /**< Performance counter 0, MALI_HW_CORE_NO_COUNTER for disabled */
	u32                  counter_src1; /**< Performance counter 1, MALI_HW_CORE_NO_COUNTER for disabled */
	u32                  last_invalidated_id;
	struct maliggy_pm_domain *pm_domain;
};

_maliggy_osk_errcode_t maliggy_l2_cache_initialize(void);
void maliggy_l2_cache_terminate(void);

struct maliggy_l2_cache_core *maliggy_l2_cache_create(_maliggy_osk_resource_t * resource);
void maliggy_l2_cache_delete(struct maliggy_l2_cache_core *cache);

MALI_STATIC_INLINE void maliggy_l2_cache_set_pm_domain(struct maliggy_l2_cache_core *cache, struct maliggy_pm_domain *domain)
{
	cache->pm_domain = domain;
}

u32 maliggy_l2_cache_get_id(struct maliggy_l2_cache_core *cache);

maliggy_bool maliggy_l2_cache_core_set_counter_src0(struct maliggy_l2_cache_core *cache, u32 counter);
maliggy_bool maliggy_l2_cache_core_set_counter_src1(struct maliggy_l2_cache_core *cache, u32 counter);
u32 maliggy_l2_cache_core_get_counter_src0(struct maliggy_l2_cache_core *cache);
u32 maliggy_l2_cache_core_get_counter_src1(struct maliggy_l2_cache_core *cache);
void maliggy_l2_cache_core_get_counter_values(struct maliggy_l2_cache_core *cache, u32 *src0, u32 *value0, u32 *src1, u32 *value1);
struct maliggy_l2_cache_core *maliggy_l2_cache_core_get_glob_l2_core(u32 index);
u32 maliggy_l2_cache_core_get_glob_num_l2_cores(void);

void maliggy_l2_cache_reset(struct maliggy_l2_cache_core *cache);
void maliggy_l2_cache_reset_all(void);

struct maliggy_group *maliggy_l2_cache_get_group(struct maliggy_l2_cache_core *cache, u32 index);

void maliggy_l2_cache_invalidate(struct maliggy_l2_cache_core *cache);
maliggy_bool maliggy_l2_cache_invalidate_conditional(struct maliggy_l2_cache_core *cache, u32 id);
void maliggy_l2_cache_invalidate_all(void);
void maliggy_l2_cache_invalidate_all_pages(u32 *pages, u32 num_pages);

maliggy_bool maliggy_l2_cache_lock_power_state(struct maliggy_l2_cache_core *cache);
void maliggy_l2_cache_unlock_power_state(struct maliggy_l2_cache_core *cache);

#endif /* __MALI_KERNEL_L2_CACHE_H__ */
