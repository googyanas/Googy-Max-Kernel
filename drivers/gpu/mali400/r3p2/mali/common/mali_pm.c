/*
 * Copyright (C) 2011-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "mali_pm.h"
#include "mali_kernel_common.h"
#include "mali_osk.h"
#include "mali_gp_scheduler.h"
#include "mali_pp_scheduler.h"
#include "mali_scheduler.h"
#include "mali_kernel_utilization.h"
#include "mali_group.h"
#include "mali_pm_domain.h"
#include "mali_pmu.h"

static maliggy_bool maliggy_power_on = MALI_FALSE;

_maliggy_osk_errcode_t maliggy_pm_initialize(void)
{
	_maliggy_osk_pm_dev_enable();
	return _MALI_OSK_ERR_OK;
}

void maliggy_pm_terminate(void)
{
	maliggy_pm_domain_terminate();
	_maliggy_osk_pm_dev_disable();
}

/* Reset GPU after power up */
static void maliggy_pm_reset_gpu(void)
{
	/* Reset all L2 caches */
	maliggy_l2_cache_reset_all();

	/* Reset all groups */
	maliggy_scheduler_reset_all_groups();
}

void maliggy_pm_os_suspend(void)
{
	MALI_DEBUG_PRINT(3, ("Mali PM: OS suspend\n"));
	maliggy_gp_scheduler_suspend();
	maliggy_pp_scheduler_suspend();
	maliggy_utilization_suspend();
/* MALI_SEC */
#if !defined(CONFIG_PM_RUNTIME)
	maliggy_group_power_off();
	maliggy_power_on = MALI_FALSE;
#endif
}

void maliggy_pm_os_resume(void)
{
#if !defined(CONFIG_PM_RUNTIME)
	struct maliggy_pmu_core *pmu = maliggy_pmu_get_global_pmu_core();
	maliggy_bool do_reset = MALI_FALSE;
#endif

	MALI_DEBUG_PRINT(3, ("Mali PM: OS resume\n"));
/* MALI_SEC */
/******************************************************************
 *
 * <2013. 08. 23>
 *  In Pegasus prime, PMU is not enabled(Power off) while
 * system wake up(suspend -> resume).
 *
 * Because PMU power is off, GPU does not work.
 * Therefore code is commented like below.
 *
 *****************************************************************/
#if !defined(CONFIG_PM_RUNTIME)
	if (MALI_TRUE != maliggy_power_on)
	{
		do_reset = MALI_TRUE;
	}

	if (NULL != pmu)
	{
		maliggy_pmu_reset(pmu);
	}

	maliggy_power_on = MALI_TRUE;
	_maliggy_osk_write_mem_barrier();

	if (do_reset)
	{
		maliggy_pm_reset_gpu();
		maliggy_group_power_on();
	}
#endif
	maliggy_gp_scheduler_resume();
	maliggy_pp_scheduler_resume();
}

void maliggy_pm_runtime_suspend(void)
{
	MALI_DEBUG_PRINT(3, ("Mali PM: Runtime suspend\n"));
	maliggy_group_power_off();
	maliggy_power_on = MALI_FALSE;
}

void maliggy_pm_runtime_resume(void)
{
	struct maliggy_pmu_core *pmu = maliggy_pmu_get_global_pmu_core();
	maliggy_bool do_reset = MALI_FALSE;

	MALI_DEBUG_PRINT(3, ("Mali PM: Runtime resume\n"));

	if (MALI_TRUE != maliggy_power_on)
	{
		do_reset = MALI_TRUE;
	}

	if (NULL != pmu)
	{
		maliggy_pmu_reset(pmu);
	}

	maliggy_power_on = MALI_TRUE;
	_maliggy_osk_write_mem_barrier();

	if (do_reset)
	{
		maliggy_pm_reset_gpu();
		maliggy_group_power_on();
	}
}

void maliggy_pm_set_power_is_on(void)
{
	maliggy_power_on = MALI_TRUE;
}

maliggy_bool maliggy_pm_is_power_on(void)
{
	return maliggy_power_on;
}
