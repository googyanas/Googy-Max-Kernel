/*
 * Copyright (C) 2011-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "mali_gp_job.h"
#include "mali_osk.h"
#include "mali_osk_list.h"
#include "mali_uk_types.h"

static u32 gp_counter_src0 = MALI_HW_CORE_NO_COUNTER;      /**< Performance counter 0, MALI_HW_CORE_NO_COUNTER for disabled */
static u32 gp_counter_src1 = MALI_HW_CORE_NO_COUNTER;		/**< Performance counter 1, MALI_HW_CORE_NO_COUNTER for disabled */

struct maliggy_gp_job *maliggy_gp_job_create(struct maliggy_session_data *session, _maliggy_uk_gp_start_job_s *uargs, u32 id)
{
	struct maliggy_gp_job *job;
	u32 perf_counter_flag;

	job = _maliggy_osk_malloc(sizeof(struct maliggy_gp_job));
	if (NULL != job)
	{
		job->finished_notification = _maliggy_osk_notification_create(_MALI_NOTIFICATION_GP_FINISHED, sizeof(_maliggy_uk_gp_job_finished_s));
		if (NULL == job->finished_notification)
		{
			_maliggy_osk_free(job);
			return NULL;
		}

		job->oom_notification = _maliggy_osk_notification_create(_MALI_NOTIFICATION_GP_STALLED, sizeof(_maliggy_uk_gp_job_suspended_s));
		if (NULL == job->oom_notification)
		{
			_maliggy_osk_notification_delete(job->finished_notification);
			_maliggy_osk_free(job);
			return NULL;
		}

		if (0 != copy_from_user(&job->uargs, uargs, sizeof(_maliggy_uk_gp_start_job_s)))
		{
			_maliggy_osk_notification_delete(job->finished_notification);
			_maliggy_osk_notification_delete(job->oom_notification);
			_maliggy_osk_free(job);
			return NULL;
		}

		perf_counter_flag = maliggy_gp_job_get_perf_counter_flag(job);

		/* case when no counters came from user space
		 * so pass the debugfs / DS-5 provided global ones to the job object */
		if (!((perf_counter_flag & _MALI_PERFORMANCE_COUNTER_FLAG_SRC0_ENABLE) ||
				(perf_counter_flag & _MALI_PERFORMANCE_COUNTER_FLAG_SRC1_ENABLE)))
		{
			maliggy_gp_job_set_perf_counter_src0(job, maliggy_gp_job_get_gp_counter_src0());
			maliggy_gp_job_set_perf_counter_src1(job, maliggy_gp_job_get_gp_counter_src1());
		}

		_maliggy_osk_list_init(&job->list);
		job->session = session;
		job->id = id;
		job->heap_current_addr = job->uargs.frame_registers[4];
		job->perf_counter_value0 = 0;
		job->perf_counter_value1 = 0;
		job->pid = _maliggy_osk_get_pid();
		job->tid = _maliggy_osk_get_tid();

		return job;
	}

	return NULL;
}

void maliggy_gp_job_delete(struct maliggy_gp_job *job)
{

	/* de-allocate the pre-allocated oom notifications */
	if (NULL != job->oom_notification)
	{
		_maliggy_osk_notification_delete(job->oom_notification);
		job->oom_notification = NULL;
	}
	if (NULL != job->finished_notification)
	{
		_maliggy_osk_notification_delete(job->finished_notification);
		job->finished_notification = NULL;
	}

	_maliggy_osk_free(job);
}

u32 maliggy_gp_job_get_gp_counter_src0(void)
{
	return gp_counter_src0;
}

maliggy_bool maliggy_gp_job_set_gp_counter_src0(u32 counter)
{
	gp_counter_src0 = counter;

	return MALI_TRUE;
}

u32 maliggy_gp_job_get_gp_counter_src1(void)
{
	return gp_counter_src1;
}

maliggy_bool maliggy_gp_job_set_gp_counter_src1(u32 counter)
{
	gp_counter_src1 = counter;

	return MALI_TRUE;
}
