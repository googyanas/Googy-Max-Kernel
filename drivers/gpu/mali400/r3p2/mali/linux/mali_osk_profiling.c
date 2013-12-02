/*
 * Copyright (C) 2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/module.h>

#include <mali_profiling_gator_api.h>
#include "mali_kernel_common.h"
#include "mali_osk.h"
#include "mali_ukk.h"
#include "mali_uk_types.h"
#include "mali_osk_profiling.h"
#include "mali_linux_trace.h"
#include "mali_gp.h"
#include "mali_pp.h"
#include "mali_pp_scheduler.h"
#include "mali_l2_cache.h"
#include "mali_user_settings_db.h"

_maliggy_osk_errcode_t _maliggy_osk_profiling_init(maliggy_bool auto_start)
{
	if (MALI_TRUE == auto_start)
	{
		maliggy_set_user_setting(_MALI_UK_USER_SETTING_SW_EVENTS_ENABLE, MALI_TRUE);
	}

	return _MALI_OSK_ERR_OK;
}

void _maliggy_osk_profiling_term(void)
{
	/* Nothing to do */
}

_maliggy_osk_errcode_t _maliggy_osk_profiling_start(u32 * limit)
{
	/* Nothing to do */
	return _MALI_OSK_ERR_OK;
}

_maliggy_osk_errcode_t _maliggy_osk_profiling_stop(u32 *count)
{
	/* Nothing to do */
	return _MALI_OSK_ERR_OK;
}

u32 _maliggy_osk_profiling_get_count(void)
{
	return 0;
}

_maliggy_osk_errcode_t _maliggy_osk_profiling_get_event(u32 index, u64* timestamp, u32* event_id, u32 data[5])
{
	/* Nothing to do */
	return _MALI_OSK_ERR_OK;
}

_maliggy_osk_errcode_t _maliggy_osk_profiling_clear(void)
{
	/* Nothing to do */
	return _MALI_OSK_ERR_OK;
}

maliggy_bool _maliggy_osk_profiling_is_recording(void)
{
	return MALI_FALSE;
}

maliggy_bool _maliggy_osk_profiling_have_recording(void)
{
	return MALI_FALSE;
}

void _maliggy_osk_profiling_report_sw_counters(u32 *counters)
{
	trace_maliggy_sw_counters(_maliggy_osk_get_pid(), _maliggy_osk_get_tid(), NULL, counters);
}


_maliggy_osk_errcode_t _maliggy_ukk_profiling_start(_maliggy_uk_profiling_start_s *args)
{
	return _maliggy_osk_profiling_start(&args->limit);
}

_maliggy_osk_errcode_t _maliggy_ukk_profiling_add_event(_maliggy_uk_profiling_add_event_s *args)
{
	/* Always add process and thread identificator in the first two data elements for events from user space */
	_maliggy_osk_profiling_add_event(args->event_id, _maliggy_osk_get_pid(), _maliggy_osk_get_tid(), args->data[2], args->data[3], args->data[4]);

	return _MALI_OSK_ERR_OK;
}

_maliggy_osk_errcode_t _maliggy_ukk_profiling_stop(_maliggy_uk_profiling_stop_s *args)
{
	return _maliggy_osk_profiling_stop(&args->count);
}

_maliggy_osk_errcode_t _maliggy_ukk_profiling_get_event(_maliggy_uk_profiling_get_event_s *args)
{
	return _maliggy_osk_profiling_get_event(args->index, &args->timestamp, &args->event_id, args->data);
}

_maliggy_osk_errcode_t _maliggy_ukk_profiling_clear(_maliggy_uk_profiling_clear_s *args)
{
	return _maliggy_osk_profiling_clear();
}

_maliggy_osk_errcode_t _maliggy_ukk_sw_counters_report(_maliggy_uk_sw_counters_report_s *args)
{
	_maliggy_osk_profiling_report_sw_counters(args->counters);
	return _MALI_OSK_ERR_OK;
}

/**
 * Called by gator.ko to set HW counters
 *
 * @param counter_id The counter ID.
 * @param event_id Event ID that the counter should count (HW counter value from TRM).
 * 
 * @return 1 on success, 0 on failure.
 */
int _maliggy_profiling_set_event(u32 counter_id, s32 event_id)
{
	if (COUNTER_VP_0_C0 == counter_id)
	{
		if (MALI_TRUE == maliggy_gp_job_set_gp_counter_src0(event_id))
		{
			return 1;
		}
	}

	if (COUNTER_VP_0_C1 == counter_id)
	{
		if (MALI_TRUE == maliggy_gp_job_set_gp_counter_src1(event_id))
		{
			return 1;
		}
	}

	if (COUNTER_FP_0_C0 == counter_id)
	{
		if (MALI_TRUE == maliggy_pp_job_set_pp_counter_src0(event_id))
		{
			return 1;
		}
	}

	if (COUNTER_FP_0_C1 == counter_id)
	{
		if (MALI_TRUE == maliggy_pp_job_set_pp_counter_src1(event_id))
		{
			return 1;
		}
	}

	if (COUNTER_L2_0_C0 <= counter_id && COUNTER_L2_2_C1 >= counter_id)
	{
		u32 core_id = (counter_id - COUNTER_L2_0_C0) >> 1;
		struct maliggy_l2_cache_core* l2_cache_core = maliggy_l2_cache_core_get_glob_l2_core(core_id);

		if (NULL != l2_cache_core)
		{
			u32 counter_src = (counter_id - COUNTER_L2_0_C0) & 1;
			if (0 == counter_src)
			{
				if (MALI_TRUE == maliggy_l2_cache_core_set_counter_src0(l2_cache_core, event_id))
				{
					return 1;
				}
			}
			else
			{
				if (MALI_TRUE == maliggy_l2_cache_core_set_counter_src1(l2_cache_core, event_id))
				{
					return 1;
				}
			}
		}
	}

	return 0;
}

/**
 * Called by gator.ko to retrieve the L2 cache counter values for all L2 cache cores.
 * The L2 cache counters are unique in that they are polled by gator, rather than being
 * transmitted via the tracepoint mechanism.
 *
 * @param values Pointer to a _maliggy_profiling_l2_counter_values structure where
 *               the counter sources and values will be output
 * @return 0 if all went well; otherwise, return the mask with the bits set for the powered off cores
 */
u32 _maliggy_profiling_get_l2_counters(_maliggy_profiling_l2_counter_values *values)
{
	struct maliggy_l2_cache_core *l2_cache;
	u32 l2_cores_num = maliggy_l2_cache_core_get_glob_num_l2_cores();
	u32 i;
	u32 ret = 0;

	MALI_DEBUG_ASSERT(l2_cores_num <= 3);

	for (i = 0; i < l2_cores_num; i++)
	{
		l2_cache = maliggy_l2_cache_core_get_glob_l2_core(i);

		if (NULL == l2_cache)
		{
			continue;
		}

		if (MALI_TRUE == maliggy_l2_cache_lock_power_state(l2_cache))
		{
			/* It is now safe to access the L2 cache core in order to retrieve the counters */
			maliggy_l2_cache_core_get_counter_values(l2_cache,
							      &values->cores[i].source0,
							      &values->cores[i].value0,
							      &values->cores[i].source1,
							      &values->cores[i].value1);
		}
		else
		{
			/* The core was not available, set the right bit in the mask. */
			ret |= (1 << i);
		}
		maliggy_l2_cache_unlock_power_state(l2_cache);
	}

	return ret;
}

/**
 * Called by gator to control the production of profiling information at runtime.
 */
void _maliggy_profiling_control(u32 action, u32 value)
{
	switch(action)
	{
	case FBDUMP_CONTROL_ENABLE:
		maliggy_set_user_setting(_MALI_UK_USER_SETTING_COLORBUFFER_CAPTURE_ENABLED, (value == 0 ? MALI_FALSE : MALI_TRUE));
		break;
	case FBDUMP_CONTROL_RATE:
		maliggy_set_user_setting(_MALI_UK_USER_SETTING_BUFFER_CAPTURE_N_FRAMES, value);
		break;
	case SW_COUNTER_ENABLE:
		maliggy_set_user_setting(_MALI_UK_USER_SETTING_SW_COUNTER_ENABLED, value);
		break;
	case FBDUMP_CONTROL_RESIZE_FACTOR:
		maliggy_set_user_setting(_MALI_UK_USER_SETTING_BUFFER_CAPTURE_RESIZE_FACTOR, value);
		break;
	default:
		break;	/* Ignore unimplemented actions */
	}
}

/**
 * Called by gator to get mali api version.
 */
u32 _maliggy_profiling_get_api_version(void)
{
	return MALI_PROFILING_API_VERSION;
}

/**
* Called by gator to get the data about Mali instance in use:
* product id, version, number of cores
*/
void _maliggy_profiling_get_maliggy_version(struct _maliggy_profiling_maliggy_version *values)
{
	values->maliggy_product_id = (u32)maliggy_kernel_core_get_product_id();
	values->maliggy_version_major = maliggy_kernel_core_get_gpu_major_version();
	values->maliggy_version_minor = maliggy_kernel_core_get_gpu_minor_version();
	values->num_of_l2_cores = maliggy_l2_cache_core_get_glob_num_l2_cores();
	values->num_of_fp_cores = maliggy_pp_scheduler_get_num_cores_total();
	values->num_of_vp_cores = 1;
}

EXPORT_SYMBOL(_maliggy_profiling_set_event);
EXPORT_SYMBOL(_maliggy_profiling_get_l2_counters);
EXPORT_SYMBOL(_maliggy_profiling_control);
EXPORT_SYMBOL(_maliggy_profiling_get_api_version);
EXPORT_SYMBOL(_maliggy_profiling_get_maliggy_version);
