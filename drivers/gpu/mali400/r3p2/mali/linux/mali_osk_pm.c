/**
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file maliggy_osk_pm.c
 * Implementation of the callback functions from common power management
 */

#include <linux/sched.h>

#ifdef CONFIG_PM_RUNTIME
#include <linux/pm_runtime.h>
#endif /* CONFIG_PM_RUNTIME */
#include <linux/platform_device.h>
#include <linux/version.h>
#include "mali_osk.h"
#include "mali_kernel_common.h"
#include "mali_kernel_linux.h"

static _maliggy_osk_atomic_t maliggy_pm_ref_count;

void _maliggy_osk_pm_dev_enable(void)
{
	_maliggy_osk_atomic_init(&maliggy_pm_ref_count, 0);
}

void _maliggy_osk_pm_dev_disable(void)
{
	_maliggy_osk_atomic_term(&maliggy_pm_ref_count);
}

/* Can NOT run in atomic context */
_maliggy_osk_errcode_t _maliggy_osk_pm_dev_ref_add(void)
{
#ifdef CONFIG_PM_RUNTIME
	int err;
	MALI_DEBUG_ASSERT_POINTER(maliggy_platform_device);
	err = pm_runtime_get_sync(&(maliggy_platform_device->dev));
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
	pm_runtime_mark_last_busy(&(maliggy_platform_device->dev));
#endif
	if (0 > err)
	{
		MALI_PRINT_ERROR(("Mali OSK PM: pm_runtime_get_sync() returned error code %d\n", err));
		return _MALI_OSK_ERR_FAULT;
	}
	_maliggy_osk_atomic_inc(&maliggy_pm_ref_count);
	MALI_DEBUG_PRINT(4, ("Mali OSK PM: Power ref taken (%u)\n", _maliggy_osk_atomic_read(&maliggy_pm_ref_count)));
#endif
	return _MALI_OSK_ERR_OK;
}

/* Can run in atomic context */
void _maliggy_osk_pm_dev_ref_dec(void)
{
#ifdef CONFIG_PM_RUNTIME
	MALI_DEBUG_ASSERT_POINTER(maliggy_platform_device);
	_maliggy_osk_atomic_dec(&maliggy_pm_ref_count);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
	pm_runtime_mark_last_busy(&(maliggy_platform_device->dev));
	pm_runtime_put_autosuspend(&(maliggy_platform_device->dev));
#else
	pm_runtime_put(&(maliggy_platform_device->dev));
#endif
	MALI_DEBUG_PRINT(4, ("Mali OSK PM: Power ref released (%u)\n", _maliggy_osk_atomic_read(&maliggy_pm_ref_count)));
#endif
}

/* Can run in atomic context */
maliggy_bool _maliggy_osk_pm_dev_ref_add_no_power_on(void)
{
#ifdef CONFIG_PM_RUNTIME
	u32 ref;
	MALI_DEBUG_ASSERT_POINTER(maliggy_platform_device);
	pm_runtime_get_noresume(&(maliggy_platform_device->dev));
	ref = _maliggy_osk_atomic_read(&maliggy_pm_ref_count);
	MALI_DEBUG_PRINT(4, ("Mali OSK PM: No-power ref taken (%u)\n", _maliggy_osk_atomic_read(&maliggy_pm_ref_count)));
	return ref > 0 ? MALI_TRUE : MALI_FALSE;
#else
	return MALI_TRUE;
#endif
}

/* Can run in atomic context */
void _maliggy_osk_pm_dev_ref_dec_no_power_on(void)
{
#ifdef CONFIG_PM_RUNTIME
	MALI_DEBUG_ASSERT_POINTER(maliggy_platform_device);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
	pm_runtime_put_autosuspend(&(maliggy_platform_device->dev));
#else
	pm_runtime_put(&(maliggy_platform_device->dev));
#endif
	MALI_DEBUG_PRINT(4, ("Mali OSK PM: No-power ref released (%u)\n", _maliggy_osk_atomic_read(&maliggy_pm_ref_count)));
#endif
}

void _maliggy_osk_pm_dev_barrier(void)
{
#ifdef CONFIG_PM_RUNTIME
	pm_runtime_barrier(&(maliggy_platform_device->dev));
#endif
}
