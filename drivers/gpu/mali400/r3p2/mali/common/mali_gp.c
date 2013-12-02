/*
 * Copyright (C) 2011-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "mali_gp.h"
#include "mali_hw_core.h"
#include "mali_group.h"
#include "mali_osk.h"
#include "regs/mali_gp_regs.h"
#include "mali_kernel_common.h"
#include "mali_kernel_core.h"
#if defined(CONFIG_MALI400_PROFILING)
#include "mali_osk_profiling.h"
#endif

static struct maliggy_gp_core *maliggy_global_gp_core = NULL;

/* Interrupt handlers */
static void maliggy_gp_irq_probe_trigger(void *data);
static _maliggy_osk_errcode_t maliggy_gp_irq_probe_ack(void *data);

struct maliggy_gp_core *maliggy_gp_create(const _maliggy_osk_resource_t * resource, struct maliggy_group *group)
{
	struct maliggy_gp_core* core = NULL;

	MALI_DEBUG_ASSERT(NULL == maliggy_global_gp_core);
	MALI_DEBUG_PRINT(2, ("Mali GP: Creating Mali GP core: %s\n", resource->description));

	core = _maliggy_osk_malloc(sizeof(struct maliggy_gp_core));
	if (NULL != core)
	{
		core->counter_src0_used = MALI_HW_CORE_NO_COUNTER;
		core->counter_src1_used = MALI_HW_CORE_NO_COUNTER;
		if (_MALI_OSK_ERR_OK == maliggy_hw_core_create(&core->hw_core, resource, MALIGP2_REGISTER_ADDRESS_SPACE_SIZE))
		{
			_maliggy_osk_errcode_t ret;

			ret = maliggy_gp_reset(core);

			if (_MALI_OSK_ERR_OK == ret)
			{
				ret = maliggy_group_add_gp_core(group, core);
				if (_MALI_OSK_ERR_OK == ret)
				{
					/* Setup IRQ handlers (which will do IRQ probing if needed) */
					core->irq = _maliggy_osk_irq_init(resource->irq,
					                               maliggy_group_upper_half_gp,
					                               group,
					                               maliggy_gp_irq_probe_trigger,
					                               maliggy_gp_irq_probe_ack,
					                               core,
					                               "mali_gp_irq_handlers");
					if (NULL != core->irq)
					{
						MALI_DEBUG_PRINT(4, ("Mali GP: set global gp core from 0x%08X to 0x%08X\n", maliggy_global_gp_core, core));
						maliggy_global_gp_core = core;

						return core;
					}
					else
					{
						MALI_PRINT_ERROR(("Mali GP: Failed to setup interrupt handlers for GP core %s\n", core->hw_core.description));
					}
					maliggy_group_remove_gp_core(group);
				}
				else
				{
					MALI_PRINT_ERROR(("Mali GP: Failed to add core %s to group\n", core->hw_core.description));
				}
			}
			maliggy_hw_core_delete(&core->hw_core);
		}

		_maliggy_osk_free(core);
	}
	else
	{
		MALI_PRINT_ERROR(("Failed to allocate memory for GP core\n"));
	}

	return NULL;
}

void maliggy_gp_delete(struct maliggy_gp_core *core)
{
	MALI_DEBUG_ASSERT_POINTER(core);

	_maliggy_osk_irq_term(core->irq);
	maliggy_hw_core_delete(&core->hw_core);
	maliggy_global_gp_core = NULL;
	_maliggy_osk_free(core);
}

void maliggy_gp_stop_bus(struct maliggy_gp_core *core)
{
	MALI_DEBUG_ASSERT_POINTER(core);

	maliggy_hw_core_register_write(&core->hw_core, MALIGP2_REG_ADDR_MGMT_CMD, MALIGP2_REG_VAL_CMD_STOP_BUS);
}

_maliggy_osk_errcode_t maliggy_gp_stop_bus_wait(struct maliggy_gp_core *core)
{
	int i;

	MALI_DEBUG_ASSERT_POINTER(core);

	/* Send the stop bus command. */
	maliggy_gp_stop_bus(core);

	/* Wait for bus to be stopped */
	for (i = 0; i < MALI_REG_POLL_COUNT_FAST; i++)
	{
		if (maliggy_hw_core_register_read(&core->hw_core, MALIGP2_REG_ADDR_MGMT_STATUS) & MALIGP2_REG_VAL_STATUS_BUS_STOPPED)
		{
			break;
		}
	}

	if (MALI_REG_POLL_COUNT_FAST == i)
	{
		MALI_PRINT_ERROR(("Mali GP: Failed to stop bus on %s\n", core->hw_core.description));
		return _MALI_OSK_ERR_FAULT;
	}
	return _MALI_OSK_ERR_OK;
}

void maliggy_gp_hard_reset(struct maliggy_gp_core *core)
{
	const u32 reset_wait_target_register = MALIGP2_REG_ADDR_MGMT_WRITE_BOUND_LOW;
	const u32 reset_invalid_value = 0xC0FFE000;
	const u32 reset_check_value = 0xC01A0000;
	const u32 reset_default_value = 0;
	int i;

	MALI_DEBUG_ASSERT_POINTER(core);
	MALI_DEBUG_PRINT(4, ("Mali GP: Hard reset of core %s\n", core->hw_core.description));

	maliggy_hw_core_register_write(&core->hw_core, reset_wait_target_register, reset_invalid_value);

	maliggy_hw_core_register_write(&core->hw_core, MALIGP2_REG_ADDR_MGMT_CMD, MALIGP2_REG_VAL_CMD_RESET);

	for (i = 0; i < MALI_REG_POLL_COUNT_FAST; i++)
	{
		maliggy_hw_core_register_write(&core->hw_core, reset_wait_target_register, reset_check_value);
		if (reset_check_value == maliggy_hw_core_register_read(&core->hw_core, reset_wait_target_register))
		{
			break;
		}
	}

	if (MALI_REG_POLL_COUNT_FAST == i)
	{
		MALI_PRINT_ERROR(("Mali GP: The hard reset loop didn't work, unable to recover\n"));
	}

	maliggy_hw_core_register_write(&core->hw_core, reset_wait_target_register, reset_default_value); /* set it back to the default */
	/* Re-enable interrupts */
	maliggy_hw_core_register_write(&core->hw_core, MALIGP2_REG_ADDR_MGMT_INT_CLEAR, MALIGP2_REG_VAL_IRQ_MASK_ALL);
	maliggy_hw_core_register_write(&core->hw_core, MALIGP2_REG_ADDR_MGMT_INT_MASK, MALIGP2_REG_VAL_IRQ_MASK_USED);

}

void maliggy_gp_reset_async(struct maliggy_gp_core *core)
{
	MALI_DEBUG_ASSERT_POINTER(core);

	MALI_DEBUG_PRINT(4, ("Mali GP: Reset of core %s\n", core->hw_core.description));

	maliggy_hw_core_register_write(&core->hw_core, MALIGP2_REG_ADDR_MGMT_INT_MASK, 0); /* disable the IRQs */
	maliggy_hw_core_register_write(&core->hw_core, MALIGP2_REG_ADDR_MGMT_INT_CLEAR, MALI400GP_REG_VAL_IRQ_RESET_COMPLETED);
	maliggy_hw_core_register_write(&core->hw_core, MALIGP2_REG_ADDR_MGMT_CMD, MALI400GP_REG_VAL_CMD_SOFT_RESET);

}

_maliggy_osk_errcode_t maliggy_gp_reset_wait(struct maliggy_gp_core *core)
{
	int i;
	u32 rawstat = 0;

	MALI_DEBUG_ASSERT_POINTER(core);

	for (i = 0; i < MALI_REG_POLL_COUNT_FAST; i++)
	{
		rawstat = maliggy_hw_core_register_read(&core->hw_core, MALIGP2_REG_ADDR_MGMT_INT_RAWSTAT);
		if (rawstat & MALI400GP_REG_VAL_IRQ_RESET_COMPLETED)
		{
			break;
		}
	}

	if (i == MALI_REG_POLL_COUNT_FAST)
	{
		MALI_PRINT_ERROR(("Mali GP: Failed to reset core %s, rawstat: 0x%08x\n",
		                 core->hw_core.description, rawstat));
		return _MALI_OSK_ERR_FAULT;
	}

	/* Re-enable interrupts */
	maliggy_hw_core_register_write(&core->hw_core, MALIGP2_REG_ADDR_MGMT_INT_CLEAR, MALIGP2_REG_VAL_IRQ_MASK_ALL);
	maliggy_hw_core_register_write(&core->hw_core, MALIGP2_REG_ADDR_MGMT_INT_MASK, MALIGP2_REG_VAL_IRQ_MASK_USED);

	return _MALI_OSK_ERR_OK;
}

_maliggy_osk_errcode_t maliggy_gp_reset(struct maliggy_gp_core *core)
{
	maliggy_gp_reset_async(core);
	return maliggy_gp_reset_wait(core);
}

void maliggy_gp_job_start(struct maliggy_gp_core *core, struct maliggy_gp_job *job)
{
	u32 startcmd = 0;
	u32 *frame_registers = maliggy_gp_job_get_frame_registers(job);

	core->counter_src0_used = maliggy_gp_job_get_perf_counter_src0(job);
	core->counter_src1_used = maliggy_gp_job_get_perf_counter_src1(job);

	MALI_DEBUG_ASSERT_POINTER(core);

	if (maliggy_gp_job_has_vs_job(job))
	{
		startcmd |= (u32) MALIGP2_REG_VAL_CMD_START_VS;
	}

	if (maliggy_gp_job_has_plbu_job(job))
	{
		startcmd |= (u32) MALIGP2_REG_VAL_CMD_START_PLBU;
	}

	MALI_DEBUG_ASSERT(0 != startcmd);

	maliggy_hw_core_register_write_array_relaxed(&core->hw_core, MALIGP2_REG_ADDR_MGMT_VSCL_START_ADDR, frame_registers, MALIGP2_NUM_REGS_FRAME);

	if (MALI_HW_CORE_NO_COUNTER != core->counter_src0_used)
	{
		maliggy_hw_core_register_write(&core->hw_core, MALIGP2_REG_ADDR_MGMT_PERF_CNT_0_SRC, core->counter_src0_used);
		maliggy_hw_core_register_write(&core->hw_core, MALIGP2_REG_ADDR_MGMT_PERF_CNT_0_ENABLE, MALIGP2_REG_VAL_PERF_CNT_ENABLE);
	}
	if (MALI_HW_CORE_NO_COUNTER != core->counter_src1_used)
	{
		maliggy_hw_core_register_write(&core->hw_core, MALIGP2_REG_ADDR_MGMT_PERF_CNT_1_SRC, core->counter_src1_used);
		maliggy_hw_core_register_write(&core->hw_core, MALIGP2_REG_ADDR_MGMT_PERF_CNT_1_ENABLE, MALIGP2_REG_VAL_PERF_CNT_ENABLE);
	}

	MALI_DEBUG_PRINT(3, ("Mali GP: Starting job (0x%08x) on core %s with command 0x%08X\n", job, core->hw_core.description, startcmd));

	/* Barrier to make sure the previous register write is finished */
	_maliggy_osk_write_mem_barrier();

	/* This is the command that starts the core. */
	maliggy_hw_core_register_write_relaxed(&core->hw_core, MALIGP2_REG_ADDR_MGMT_CMD, startcmd);

	/* Barrier to make sure the previous register write is finished */
	_maliggy_osk_write_mem_barrier();
}

void maliggy_gp_resume_with_new_heap(struct maliggy_gp_core *core, u32 start_addr, u32 end_addr)
{
	u32 irq_readout;

	MALI_DEBUG_ASSERT_POINTER(core);

	irq_readout = maliggy_hw_core_register_read(&core->hw_core, MALIGP2_REG_ADDR_MGMT_INT_RAWSTAT);

	if (irq_readout & MALIGP2_REG_VAL_IRQ_PLBU_OUT_OF_MEM)
	{
		maliggy_hw_core_register_write(&core->hw_core, MALIGP2_REG_ADDR_MGMT_INT_CLEAR, (MALIGP2_REG_VAL_IRQ_PLBU_OUT_OF_MEM | MALIGP2_REG_VAL_IRQ_HANG));
		maliggy_hw_core_register_write(&core->hw_core, MALIGP2_REG_ADDR_MGMT_INT_MASK, MALIGP2_REG_VAL_IRQ_MASK_USED); /* re-enable interrupts */
		maliggy_hw_core_register_write_relaxed(&core->hw_core, MALIGP2_REG_ADDR_MGMT_PLBU_ALLOC_START_ADDR, start_addr);
		maliggy_hw_core_register_write_relaxed(&core->hw_core, MALIGP2_REG_ADDR_MGMT_PLBU_ALLOC_END_ADDR, end_addr);

		MALI_DEBUG_PRINT(3, ("Mali GP: Resuming job\n"));

		maliggy_hw_core_register_write(&core->hw_core, MALIGP2_REG_ADDR_MGMT_CMD, MALIGP2_REG_VAL_CMD_UPDATE_PLBU_ALLOC);
		_maliggy_osk_write_mem_barrier();
	}
	/*
	 * else: core has been reset between PLBU_OUT_OF_MEM interrupt and this new heap response.
	 * A timeout or a page fault on Mali-200 PP core can cause this behaviour.
	 */
}

u32 maliggy_gp_core_get_version(struct maliggy_gp_core *core)
{
	MALI_DEBUG_ASSERT_POINTER(core);
	return maliggy_hw_core_register_read(&core->hw_core, MALIGP2_REG_ADDR_MGMT_VERSION);
}

struct maliggy_gp_core *maliggy_gp_get_global_gp_core(void)
{
	return maliggy_global_gp_core;
}

/* ------------- interrupt handling below ------------------ */
static void maliggy_gp_irq_probe_trigger(void *data)
{
	struct maliggy_gp_core *core = (struct maliggy_gp_core *)data;

	maliggy_hw_core_register_write(&core->hw_core, MALIGP2_REG_ADDR_MGMT_INT_MASK, MALIGP2_REG_VAL_IRQ_MASK_USED);
	maliggy_hw_core_register_write(&core->hw_core, MALIGP2_REG_ADDR_MGMT_INT_RAWSTAT, MALIGP2_REG_VAL_CMD_FORCE_HANG);
	_maliggy_osk_mem_barrier();
}

static _maliggy_osk_errcode_t maliggy_gp_irq_probe_ack(void *data)
{
	struct maliggy_gp_core *core = (struct maliggy_gp_core *)data;
	u32 irq_readout;

	irq_readout = maliggy_hw_core_register_read(&core->hw_core, MALIGP2_REG_ADDR_MGMT_INT_STAT);
	if (MALIGP2_REG_VAL_IRQ_FORCE_HANG & irq_readout)
	{
		maliggy_hw_core_register_write(&core->hw_core, MALIGP2_REG_ADDR_MGMT_INT_CLEAR, MALIGP2_REG_VAL_IRQ_FORCE_HANG);
		_maliggy_osk_mem_barrier();
		return _MALI_OSK_ERR_OK;
	}

	return _MALI_OSK_ERR_FAULT;
}

/* ------ local helper functions below --------- */
#if MALI_STATE_TRACKING
u32 maliggy_gp_dumpggy_state(struct maliggy_gp_core *core, char *buf, u32 size)
{
	int n = 0;

	n += _maliggy_osk_snprintf(buf + n, size - n, "\tGP: %s\n", core->hw_core.description);

	return n;
}
#endif

void maliggy_gp_update_performance_counters(struct maliggy_gp_core *core, struct maliggy_gp_job *job, maliggy_bool suspend)
{
	u32 val0 = 0;
	u32 val1 = 0;

	if (MALI_HW_CORE_NO_COUNTER != core->counter_src0_used)
	{
		val0 = maliggy_hw_core_register_read(&core->hw_core, MALIGP2_REG_ADDR_MGMT_PERF_CNT_0_VALUE);
		maliggy_gp_job_set_perf_counter_value0(job, val0);

#if defined(CONFIG_MALI400_PROFILING)
		_maliggy_osk_profiling_report_hw_counter(COUNTER_VP_0_C0, val0);
#endif

	}

	if (MALI_HW_CORE_NO_COUNTER != core->counter_src1_used)
	{
		val1 = maliggy_hw_core_register_read(&core->hw_core, MALIGP2_REG_ADDR_MGMT_PERF_CNT_1_VALUE);
		maliggy_gp_job_set_perf_counter_value1(job, val1);

#if defined(CONFIG_MALI400_PROFILING)
		_maliggy_osk_profiling_report_hw_counter(COUNTER_VP_0_C1, val1);
#endif
	}
}
