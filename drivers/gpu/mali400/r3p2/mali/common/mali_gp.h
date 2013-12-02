/*
 * Copyright (C) 2011-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_GP_H__
#define __MALI_GP_H__

#include "mali_osk.h"
#include "mali_gp_job.h"
#include "mali_hw_core.h"
#include "regs/mali_gp_regs.h"

struct maliggy_group;

/**
 * Definition of the GP core struct
 * Used to track a GP core in the system.
 */
struct maliggy_gp_core
{
	struct maliggy_hw_core  hw_core;           /**< Common for all HW cores */
	_maliggy_osk_irq_t     *irq;               /**< IRQ handler */
	u32                  counter_src0_used; /**< The selected performance counter 0 when a job is running */
	u32                  counter_src1_used; /**< The selected performance counter 1 when a job is running */
};

_maliggy_osk_errcode_t maliggy_gp_initialize(void);
void maliggy_gp_terminate(void);

struct maliggy_gp_core *maliggy_gp_create(const _maliggy_osk_resource_t * resource, struct maliggy_group *group);
void maliggy_gp_delete(struct maliggy_gp_core *core);

void maliggy_gp_stop_bus(struct maliggy_gp_core *core);
_maliggy_osk_errcode_t maliggy_gp_stop_bus_wait(struct maliggy_gp_core *core);
void maliggy_gp_reset_async(struct maliggy_gp_core *core);
_maliggy_osk_errcode_t maliggy_gp_reset_wait(struct maliggy_gp_core *core);
void maliggy_gp_hard_reset(struct maliggy_gp_core *core);
_maliggy_osk_errcode_t maliggy_gp_reset(struct maliggy_gp_core *core);

void maliggy_gp_job_start(struct maliggy_gp_core *core, struct maliggy_gp_job *job);
void maliggy_gp_resume_with_new_heap(struct maliggy_gp_core *core, u32 start_addr, u32 end_addr);

u32 maliggy_gp_core_get_version(struct maliggy_gp_core *core);

struct maliggy_gp_core *maliggy_gp_get_global_gp_core(void);

u32 maliggy_gp_dumpggy_state(struct maliggy_gp_core *core, char *buf, u32 size);

void maliggy_gp_update_performance_counters(struct maliggy_gp_core *core, struct maliggy_gp_job *job, maliggy_bool suspend);

/*** Accessor functions ***/
MALI_STATIC_INLINE const char *maliggy_gp_get_hw_core_desc(struct maliggy_gp_core *core)
{
	return core->hw_core.description;
}

/*** Register reading/writing functions ***/
MALI_STATIC_INLINE u32 maliggy_gp_get_int_stat(struct maliggy_gp_core *core)
{
	return maliggy_hw_core_register_read(&core->hw_core, MALIGP2_REG_ADDR_MGMT_INT_STAT);
}

MALI_STATIC_INLINE void maliggy_gp_mask_all_interrupts(struct maliggy_gp_core *core)
{
	maliggy_hw_core_register_write(&core->hw_core, MALIGP2_REG_ADDR_MGMT_INT_MASK, MALIGP2_REG_VAL_IRQ_MASK_NONE);
}

MALI_STATIC_INLINE u32 maliggy_gp_read_rawstat(struct maliggy_gp_core *core)
{
	return maliggy_hw_core_register_read(&core->hw_core, MALIGP2_REG_ADDR_MGMT_INT_RAWSTAT) & MALIGP2_REG_VAL_IRQ_MASK_USED;
}

MALI_STATIC_INLINE u32 maliggy_gp_read_core_status(struct maliggy_gp_core *core)
{
	return maliggy_hw_core_register_read(&core->hw_core, MALIGP2_REG_ADDR_MGMT_STATUS);
}

MALI_STATIC_INLINE void maliggy_gp_enable_interrupts(struct maliggy_gp_core *core, u32 irq_exceptions)
{
	/* Enable all interrupts, except those specified in irq_exceptions */
	maliggy_hw_core_register_write(&core->hw_core, MALIGP2_REG_ADDR_MGMT_INT_MASK,
	                            MALIGP2_REG_VAL_IRQ_MASK_USED & ~irq_exceptions);
}

MALI_STATIC_INLINE u32 maliggy_gp_read_plbu_alloc_start_addr(struct maliggy_gp_core *core)
{
	return maliggy_hw_core_register_read(&core->hw_core, MALIGP2_REG_ADDR_MGMT_PLBU_ALLOC_START_ADDR);
}

#endif /* __MALI_GP_H__ */
