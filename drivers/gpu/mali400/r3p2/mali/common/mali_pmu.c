/*
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file maliggy_pmu.c
 * Mali driver functions for Mali 400 PMU hardware
 */
#include "mali_hw_core.h"
#include "mali_pmu.h"
#include "mali_pp.h"
#include "mali_kernel_common.h"
#include "mali_osk.h"
#include "mali_pm.h"
#include "mali_osk_mali.h"

static u32 maliggy_pmu_detect_mask(u32 number_of_pp_cores, u32 number_of_l2_caches);

/** @brief MALI inbuilt PMU hardware info and PMU hardware has knowledge of cores power mask
 */
struct maliggy_pmu_core
{
	struct maliggy_hw_core hw_core;
	_maliggy_osk_lock_t *lock;
	u32 registered_cores_mask;
	u32 active_cores_mask;
	u32 switch_delay;
};

static struct maliggy_pmu_core *maliggy_global_pmu_core = NULL;

/** @brief Register layout for hardware PMU
 */
typedef enum {
	PMU_REG_ADDR_MGMT_POWER_UP                  = 0x00,     /*< Power up register */
	PMU_REG_ADDR_MGMT_POWER_DOWN                = 0x04,     /*< Power down register */
	PMU_REG_ADDR_MGMT_STATUS                    = 0x08,     /*< Core sleep status register */
	PMU_REG_ADDR_MGMT_INT_MASK                  = 0x0C,     /*< Interrupt mask register */
	PMU_REG_ADDR_MGMT_INT_RAWSTAT               = 0x10,     /*< Interrupt raw status register */
	PMU_REG_ADDR_MGMT_INT_CLEAR                 = 0x18,     /*< Interrupt clear register */
	PMU_REG_ADDR_MGMT_SW_DELAY                  = 0x1C,     /*< Switch delay register */
	PMU_REGISTER_ADDRESS_SPACE_SIZE             = 0x28,     /*< Size of register space */
} pmu_reg_addr_mgmt_addr;

#define PMU_REG_VAL_IRQ 1

struct maliggy_pmu_core *maliggy_pmu_create(_maliggy_osk_resource_t *resource, u32 number_of_pp_cores, u32 number_of_l2_caches)
{
	struct maliggy_pmu_core* pmu;

	MALI_DEBUG_ASSERT(NULL == maliggy_global_pmu_core);
	MALI_DEBUG_PRINT(2, ("Mali PMU: Creating Mali PMU core\n"));

	pmu = (struct maliggy_pmu_core *)_maliggy_osk_malloc(sizeof(struct maliggy_pmu_core));
	if (NULL != pmu)
	{
		pmu->lock = _maliggy_osk_lock_init(_MALI_OSK_LOCKFLAG_SPINLOCK | _MALI_OSK_LOCKFLAG_NONINTERRUPTABLE,
		                                0, _MALI_OSK_LOCK_ORDER_PMU);
		if (NULL != pmu->lock)
		{
			pmu->registered_cores_mask = maliggy_pmu_detect_mask(number_of_pp_cores, number_of_l2_caches);
			pmu->active_cores_mask = pmu->registered_cores_mask;

			if (_MALI_OSK_ERR_OK == maliggy_hw_core_create(&pmu->hw_core, resource, PMU_REGISTER_ADDRESS_SPACE_SIZE))
			{
				_maliggy_osk_errcode_t err;
				struct _maliggy_osk_device_data data = { 0, };

				err = _maliggy_osk_device_data_get(&data);
				if (_MALI_OSK_ERR_OK == err)
				{
					pmu->switch_delay = data.pmu_switch_delay;
					maliggy_global_pmu_core = pmu;
					return pmu;
				}
				maliggy_hw_core_delete(&pmu->hw_core);
			}
			_maliggy_osk_lock_term(pmu->lock);
		}
		_maliggy_osk_free(pmu);
	}

	return NULL;
}

void maliggy_pmu_delete(struct maliggy_pmu_core *pmu)
{
	MALI_DEBUG_ASSERT_POINTER(pmu);
	MALI_DEBUG_ASSERT(pmu == maliggy_global_pmu_core);
	MALI_DEBUG_PRINT(2, ("Mali PMU: Deleting Mali PMU core\n"));

	_maliggy_osk_lock_term(pmu->lock);
	maliggy_hw_core_delete(&pmu->hw_core);
	_maliggy_osk_free(pmu);
	maliggy_global_pmu_core = NULL;
}

static void maliggy_pmu_lock(struct maliggy_pmu_core *pmu)
{
	_maliggy_osk_lock_wait(pmu->lock, _MALI_OSK_LOCKMODE_RW);
}
static void maliggy_pmu_unlock(struct maliggy_pmu_core *pmu)
{
	_maliggy_osk_lock_signal(pmu->lock, _MALI_OSK_LOCKMODE_RW);
}

static _maliggy_osk_errcode_t maliggy_pmu_send_command_internal(struct maliggy_pmu_core *pmu, const u32 command, const u32 mask)
{
	u32 rawstat;
	u32 timeout = MALI_REG_POLL_COUNT_SLOW;

	MALI_DEBUG_ASSERT_POINTER(pmu);
	MALI_DEBUG_ASSERT(0 == (maliggy_hw_core_register_read(&pmu->hw_core, PMU_REG_ADDR_MGMT_INT_RAWSTAT)
	                        & PMU_REG_VAL_IRQ));

	maliggy_hw_core_register_write(&pmu->hw_core, command, mask);

	/* Wait for the command to complete */
	do
	{
		rawstat = maliggy_hw_core_register_read(&pmu->hw_core, PMU_REG_ADDR_MGMT_INT_RAWSTAT);
		--timeout;
	} while (0 == (rawstat & PMU_REG_VAL_IRQ) && 0 < timeout);

	MALI_DEBUG_ASSERT(0 < timeout);
	if (0 == timeout)
	{
		return _MALI_OSK_ERR_TIMEOUT;
	}

	maliggy_hw_core_register_write(&pmu->hw_core, PMU_REG_ADDR_MGMT_INT_CLEAR, PMU_REG_VAL_IRQ);

	return _MALI_OSK_ERR_OK;
}

static _maliggy_osk_errcode_t maliggy_pmu_send_command(struct maliggy_pmu_core *pmu, const u32 command, const u32 mask)
{
	u32 stat;

	if (0 == mask) return _MALI_OSK_ERR_OK;

	stat = maliggy_hw_core_register_read(&pmu->hw_core, PMU_REG_ADDR_MGMT_STATUS);
	stat &= pmu->registered_cores_mask;

	switch (command)
	{
		case PMU_REG_ADDR_MGMT_POWER_DOWN:
			if (mask == stat) return _MALI_OSK_ERR_OK;
			break;
		case PMU_REG_ADDR_MGMT_POWER_UP:
			if (0 == (stat & mask)) return _MALI_OSK_ERR_OK;
			break;
		default:
			MALI_DEBUG_ASSERT(0);
			break;
	}

	maliggy_pmu_send_command_internal(pmu, command, mask);

#if defined(DEBUG)
	{
		/* Get power status of cores */
		stat = maliggy_hw_core_register_read(&pmu->hw_core, PMU_REG_ADDR_MGMT_STATUS);
		stat &= pmu->registered_cores_mask;

		switch (command)
		{
			case PMU_REG_ADDR_MGMT_POWER_DOWN:
				MALI_DEBUG_ASSERT(mask == (stat & mask));
				MALI_DEBUG_ASSERT(0 == (stat & pmu->active_cores_mask));
				MALI_DEBUG_ASSERT((pmu->registered_cores_mask & ~pmu->active_cores_mask) == stat);
				break;
			case PMU_REG_ADDR_MGMT_POWER_UP:
				MALI_DEBUG_ASSERT(0 == (stat & mask));
				MALI_DEBUG_ASSERT(0 == (stat & pmu->active_cores_mask));
				break;
			default:
				MALI_DEBUG_ASSERT(0);
				break;
		}
	}
#endif /* defined(DEBUG) */

	return _MALI_OSK_ERR_OK;
}

_maliggy_osk_errcode_t maliggy_pmu_reset(struct maliggy_pmu_core *pmu)
{
	_maliggy_osk_errcode_t err;
	u32 cores_off_mask, cores_on_mask, stat;

	maliggy_pmu_lock(pmu);

	/* Setup the desired defaults */
	maliggy_hw_core_register_write_relaxed(&pmu->hw_core, PMU_REG_ADDR_MGMT_INT_MASK, 0);
	maliggy_hw_core_register_write_relaxed(&pmu->hw_core, PMU_REG_ADDR_MGMT_SW_DELAY, pmu->switch_delay);

	/* Get power status of cores */
	stat = maliggy_hw_core_register_read(&pmu->hw_core, PMU_REG_ADDR_MGMT_STATUS);

	cores_off_mask = pmu->registered_cores_mask & ~(stat | pmu->active_cores_mask);
	cores_on_mask  = pmu->registered_cores_mask &  (stat & pmu->active_cores_mask);

	if (0 != cores_off_mask)
	{
		err = maliggy_pmu_send_command_internal(pmu, PMU_REG_ADDR_MGMT_POWER_DOWN, cores_off_mask);
		if (_MALI_OSK_ERR_OK != err) return err;
	}

	if (0 != cores_on_mask)
	{
		err = maliggy_pmu_send_command_internal(pmu, PMU_REG_ADDR_MGMT_POWER_UP, cores_on_mask);
		if (_MALI_OSK_ERR_OK != err) return err;
	}

#if defined(DEBUG)
	{
		stat = maliggy_hw_core_register_read(&pmu->hw_core, PMU_REG_ADDR_MGMT_STATUS);
		stat &= pmu->registered_cores_mask;

		MALI_DEBUG_ASSERT(stat == (pmu->registered_cores_mask & ~pmu->active_cores_mask));
	}
#endif /* defined(DEBUG) */

	maliggy_pmu_unlock(pmu);

	return _MALI_OSK_ERR_OK;
}

_maliggy_osk_errcode_t maliggy_pmu_power_down(struct maliggy_pmu_core *pmu, u32 mask)
{
	_maliggy_osk_errcode_t err;

	MALI_DEBUG_ASSERT_POINTER(pmu);
	MALI_DEBUG_ASSERT(pmu->registered_cores_mask != 0 );

	/* Make sure we have a valid power domain mask */
	if (mask > pmu->registered_cores_mask)
	{
		return _MALI_OSK_ERR_INVALID_ARGS;
	}

	maliggy_pmu_lock(pmu);

	MALI_DEBUG_PRINT(4, ("Mali PMU: Power down (0x%08X)\n", mask));

	pmu->active_cores_mask &= ~mask;

	_maliggy_osk_pm_dev_ref_add_no_power_on();
	if (!maliggy_pm_is_power_on())
	{
		/* Don't touch hardware if all of Mali is powered off. */
		_maliggy_osk_pm_dev_ref_dec_no_power_on();
		maliggy_pmu_unlock(pmu);

		MALI_DEBUG_PRINT(4, ("Mali PMU: Skipping power down (0x%08X) since Mali is off\n", mask));

		return _MALI_OSK_ERR_BUSY;
	}

	err = maliggy_pmu_send_command(pmu, PMU_REG_ADDR_MGMT_POWER_DOWN, mask);

	_maliggy_osk_pm_dev_ref_dec_no_power_on();
	maliggy_pmu_unlock(pmu);

	return err;
}

_maliggy_osk_errcode_t maliggy_pmu_power_up(struct maliggy_pmu_core *pmu, u32 mask)
{
	_maliggy_osk_errcode_t err;

	MALI_DEBUG_ASSERT_POINTER(pmu);
	MALI_DEBUG_ASSERT(pmu->registered_cores_mask != 0 );

	/* Make sure we have a valid power domain mask */
	if (mask & ~pmu->registered_cores_mask)
	{
		return _MALI_OSK_ERR_INVALID_ARGS;
	}

	maliggy_pmu_lock(pmu);

	MALI_DEBUG_PRINT(4, ("Mali PMU: Power up (0x%08X)\n", mask));

	pmu->active_cores_mask |= mask;

	_maliggy_osk_pm_dev_ref_add_no_power_on();
	if (!maliggy_pm_is_power_on())
	{
		/* Don't touch hardware if all of Mali is powered off. */
		_maliggy_osk_pm_dev_ref_dec_no_power_on();
		maliggy_pmu_unlock(pmu);

		MALI_DEBUG_PRINT(4, ("Mali PMU: Skipping power up (0x%08X) since Mali is off\n", mask));

		return _MALI_OSK_ERR_BUSY;
	}

	err = maliggy_pmu_send_command(pmu, PMU_REG_ADDR_MGMT_POWER_UP, mask);

	_maliggy_osk_pm_dev_ref_dec_no_power_on();
	maliggy_pmu_unlock(pmu);

	return err;
}

_maliggy_osk_errcode_t maliggy_pmu_power_down_all(struct maliggy_pmu_core *pmu)
{
	_maliggy_osk_errcode_t err;

	MALI_DEBUG_ASSERT_POINTER(pmu);
	MALI_DEBUG_ASSERT(pmu->registered_cores_mask != 0);

	maliggy_pmu_lock(pmu);

	/* Setup the desired defaults in case we were called before maliggy_pmu_reset() */
	maliggy_hw_core_register_write_relaxed(&pmu->hw_core, PMU_REG_ADDR_MGMT_INT_MASK, 0);
	maliggy_hw_core_register_write_relaxed(&pmu->hw_core, PMU_REG_ADDR_MGMT_SW_DELAY, pmu->switch_delay);

	err = maliggy_pmu_send_command(pmu, PMU_REG_ADDR_MGMT_POWER_DOWN, pmu->registered_cores_mask);

	maliggy_pmu_unlock(pmu);

	return err;
}

_maliggy_osk_errcode_t maliggy_pmu_power_up_all(struct maliggy_pmu_core *pmu)
{
	_maliggy_osk_errcode_t err;

	MALI_DEBUG_ASSERT_POINTER(pmu);
	MALI_DEBUG_ASSERT(pmu->registered_cores_mask != 0);

	maliggy_pmu_lock(pmu);

	/* Setup the desired defaults in case we were called before maliggy_pmu_reset() */
	maliggy_hw_core_register_write_relaxed(&pmu->hw_core, PMU_REG_ADDR_MGMT_INT_MASK, 0);
	maliggy_hw_core_register_write_relaxed(&pmu->hw_core, PMU_REG_ADDR_MGMT_SW_DELAY, pmu->switch_delay);

	err = maliggy_pmu_send_command(pmu, PMU_REG_ADDR_MGMT_POWER_UP, pmu->active_cores_mask);

	maliggy_pmu_unlock(pmu);
	return err;
}

struct maliggy_pmu_core *maliggy_pmu_get_global_pmu_core(void)
{
	return maliggy_global_pmu_core;
}

static u32 maliggy_pmu_detect_mask(u32 number_of_pp_cores, u32 number_of_l2_caches)
{
	u32 mask = 0;

	if (number_of_l2_caches == 1)
	{
		/* Mali-300 or Mali-400 */
		u32 i;

		/* GP */
		mask = 0x01;

		/* L2 cache */
		mask |= 0x01<<1;

		/* Set bit for each PP core */
		for (i = 0; i < number_of_pp_cores; i++)
		{
			mask |= 0x01<<(i+2);
		}
	}
	else if (number_of_l2_caches > 1)
	{
		/* Mali-450 */

		/* GP (including its L2 cache) */
		mask = 0x01;

		/* There is always at least one PP (including its L2 cache) */
		mask |= 0x01<<1;

		/* Additional PP cores in same L2 cache */
		if (number_of_pp_cores >= 2)
		{
			mask |= 0x01<<2;
		}

		/* Additional PP cores in a third L2 cache */
		if (number_of_pp_cores >= 5)
		{
			mask |= 0x01<<3;
		}
	}

	MALI_DEBUG_PRINT(4, ("Mali PMU: Power mask is 0x%08X (%u + %u)\n", mask, number_of_pp_cores, number_of_l2_caches));

	return mask;
}
