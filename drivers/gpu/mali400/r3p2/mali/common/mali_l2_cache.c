/*
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include "mali_kernel_common.h"
#include "mali_osk.h"
#include "mali_l2_cache.h"
#include "mali_hw_core.h"
#include "mali_scheduler.h"
#include "mali_pm_domain.h"

/**
 * Size of the Mali L2 cache registers in bytes
 */
#define MALI400_L2_CACHE_REGISTERS_SIZE 0x30

/**
 * Mali L2 cache register numbers
 * Used in the register read/write routines.
 * See the hardware documentation for more information about each register
 */
typedef enum maliggy_l2_cache_register {
	MALI400_L2_CACHE_REGISTER_STATUS       = 0x0008,
	/*unused                               = 0x000C */
	MALI400_L2_CACHE_REGISTER_COMMAND      = 0x0010, /**< Misc cache commands, e.g. clear */
	MALI400_L2_CACHE_REGISTER_CLEAR_PAGE   = 0x0014,
	MALI400_L2_CACHE_REGISTER_MAX_READS    = 0x0018, /**< Limit of outstanding read requests */
	MALI400_L2_CACHE_REGISTER_ENABLE       = 0x001C, /**< Enable misc cache features */
	MALI400_L2_CACHE_REGISTER_PERFCNT_SRC0 = 0x0020,
	MALI400_L2_CACHE_REGISTER_PERFCNT_VAL0 = 0x0024,
	MALI400_L2_CACHE_REGISTER_PERFCNT_SRC1 = 0x0028,
	MALI400_L2_CACHE_REGISTER_PERFCNT_VAL1 = 0x002C,
} maliggy_l2_cache_register;

/**
 * Mali L2 cache commands
 * These are the commands that can be sent to the Mali L2 cache unit
 */
typedef enum maliggy_l2_cache_command
{
	MALI400_L2_CACHE_COMMAND_CLEAR_ALL = 0x01, /**< Clear the entire cache */
	/* Read HW TRM carefully before adding/using other commands than the clear above */
} maliggy_l2_cache_command;

/**
 * Mali L2 cache commands
 * These are the commands that can be sent to the Mali L2 cache unit
 */
typedef enum maliggy_l2_cache_enable
{
	MALI400_L2_CACHE_ENABLE_DEFAULT = 0x0, /**< Default state of enable register */
	MALI400_L2_CACHE_ENABLE_ACCESS = 0x01, /**< Permit cacheable accesses */
	MALI400_L2_CACHE_ENABLE_READ_ALLOCATE = 0x02, /**< Permit cache read allocate */
} maliggy_l2_cache_enable;

/**
 * Mali L2 cache status bits
 */
typedef enum maliggy_l2_cache_status
{
	MALI400_L2_CACHE_STATUS_COMMAND_BUSY = 0x01, /**< Command handler of L2 cache is busy */
	MALI400_L2_CACHE_STATUS_DATA_BUSY    = 0x02, /**< L2 cache is busy handling data requests */
} maliggy_l2_cache_status;

#define MALI400_L2_MAX_READS_DEFAULT 0x1C

static struct maliggy_l2_cache_core *maliggy_global_l2_cache_cores[MALI_MAX_NUMBER_OF_L2_CACHE_CORES] = { NULL, };
static u32 maliggy_global_num_l2_cache_cores = 0;

int maliggy_l2_max_reads = MALI400_L2_MAX_READS_DEFAULT;

/* Local helper functions */
static _maliggy_osk_errcode_t maliggy_l2_cache_send_command(struct maliggy_l2_cache_core *cache, u32 reg, u32 val);


struct maliggy_l2_cache_core *maliggy_l2_cache_create(_maliggy_osk_resource_t *resource)
{
	struct maliggy_l2_cache_core *cache = NULL;
	_maliggy_osk_lock_flags_t lock_flags;

#if defined(MALI_UPPER_HALF_SCHEDULING)
	lock_flags = _MALI_OSK_LOCKFLAG_ORDERED | _MALI_OSK_LOCKFLAG_SPINLOCK_IRQ | _MALI_OSK_LOCKFLAG_NONINTERRUPTABLE;
#else
	lock_flags = _MALI_OSK_LOCKFLAG_ORDERED | _MALI_OSK_LOCKFLAG_SPINLOCK | _MALI_OSK_LOCKFLAG_NONINTERRUPTABLE;
#endif

	MALI_DEBUG_PRINT(2, ("Mali L2 cache: Creating Mali L2 cache: %s\n", resource->description));

	if (maliggy_global_num_l2_cache_cores >= MALI_MAX_NUMBER_OF_L2_CACHE_CORES)
	{
		MALI_PRINT_ERROR(("Mali L2 cache: Too many L2 cache core objects created\n"));
		return NULL;
	}

	cache = _maliggy_osk_malloc(sizeof(struct maliggy_l2_cache_core));
	if (NULL != cache)
	{
		cache->core_id =  maliggy_global_num_l2_cache_cores;
		cache->counter_src0 = MALI_HW_CORE_NO_COUNTER;
		cache->counter_src1 = MALI_HW_CORE_NO_COUNTER;
		cache->pm_domain = NULL;
		if (_MALI_OSK_ERR_OK == maliggy_hw_core_create(&cache->hw_core, resource, MALI400_L2_CACHE_REGISTERS_SIZE))
		{
			cache->command_lock = _maliggy_osk_lock_init(lock_flags, 0, _MALI_OSK_LOCK_ORDER_L2_COMMAND);
			if (NULL != cache->command_lock)
			{
				cache->counter_lock = _maliggy_osk_lock_init(lock_flags, 0, _MALI_OSK_LOCK_ORDER_L2_COUNTER);
				if (NULL != cache->counter_lock)
				{
					maliggy_l2_cache_reset(cache);

					cache->last_invalidated_id = 0;

					maliggy_global_l2_cache_cores[maliggy_global_num_l2_cache_cores] = cache;
					maliggy_global_num_l2_cache_cores++;

					return cache;
				}
				else
				{
					MALI_PRINT_ERROR(("Mali L2 cache: Failed to create counter lock for L2 cache core %s\n", cache->hw_core.description));
				}

				_maliggy_osk_lock_term(cache->command_lock);
			}
			else
			{
				MALI_PRINT_ERROR(("Mali L2 cache: Failed to create command lock for L2 cache core %s\n", cache->hw_core.description));
			}

			maliggy_hw_core_delete(&cache->hw_core);
		}

		_maliggy_osk_free(cache);
	}
	else
	{
		MALI_PRINT_ERROR(("Mali L2 cache: Failed to allocate memory for L2 cache core\n"));
	}

	return NULL;
}

void maliggy_l2_cache_delete(struct maliggy_l2_cache_core *cache)
{
	u32 i;

	/* reset to defaults */
	maliggy_hw_core_register_write(&cache->hw_core, MALI400_L2_CACHE_REGISTER_MAX_READS, (u32)MALI400_L2_MAX_READS_DEFAULT);
	maliggy_hw_core_register_write(&cache->hw_core, MALI400_L2_CACHE_REGISTER_ENABLE, (u32)MALI400_L2_CACHE_ENABLE_DEFAULT);

	_maliggy_osk_lock_term(cache->counter_lock);
	_maliggy_osk_lock_term(cache->command_lock);
	maliggy_hw_core_delete(&cache->hw_core);

	for (i = 0; i < maliggy_global_num_l2_cache_cores; i++)
	{
		if (maliggy_global_l2_cache_cores[i] == cache)
		{
			maliggy_global_l2_cache_cores[i] = NULL;
			maliggy_global_num_l2_cache_cores--;

			if (i != maliggy_global_num_l2_cache_cores)
			{
				/* We removed a l2 cache from the middle of the array -- move the last
				 * l2 cache to the current position to close the gap */
				maliggy_global_l2_cache_cores[i] = maliggy_global_l2_cache_cores[maliggy_global_num_l2_cache_cores];
				maliggy_global_l2_cache_cores[maliggy_global_num_l2_cache_cores] = NULL;
			}

			break;
		}
	}

	_maliggy_osk_free(cache);
}

u32 maliggy_l2_cache_get_id(struct maliggy_l2_cache_core *cache)
{
	return cache->core_id;
}

maliggy_bool maliggy_l2_cache_core_set_counter_src0(struct maliggy_l2_cache_core *cache, u32 counter)
{
	u32 value = 0; /* disabled src */
	maliggy_bool core_is_on;

	MALI_DEBUG_ASSERT_POINTER(cache);

	core_is_on = maliggy_l2_cache_lock_power_state(cache);

	_maliggy_osk_lock_wait(cache->counter_lock, _MALI_OSK_LOCKMODE_RW);

	cache->counter_src0 = counter;

	if (MALI_HW_CORE_NO_COUNTER != counter)
	{
		value = counter;
	}

	if (MALI_TRUE == core_is_on)
	{
		maliggy_hw_core_register_write(&cache->hw_core, MALI400_L2_CACHE_REGISTER_PERFCNT_SRC0, value);
	}

	_maliggy_osk_lock_signal(cache->counter_lock, _MALI_OSK_LOCKMODE_RW);

	maliggy_l2_cache_unlock_power_state(cache);

	return MALI_TRUE;
}

maliggy_bool maliggy_l2_cache_core_set_counter_src1(struct maliggy_l2_cache_core *cache, u32 counter)
{
	u32 value = 0; /* disabled src */
	maliggy_bool core_is_on;

	MALI_DEBUG_ASSERT_POINTER(cache);

	core_is_on = maliggy_l2_cache_lock_power_state(cache);

	_maliggy_osk_lock_wait(cache->counter_lock, _MALI_OSK_LOCKMODE_RW);

	cache->counter_src1 = counter;

	if (MALI_HW_CORE_NO_COUNTER != counter)
	{
		value = counter;
	}

	if (MALI_TRUE == core_is_on)
	{
		maliggy_hw_core_register_write(&cache->hw_core, MALI400_L2_CACHE_REGISTER_PERFCNT_SRC1, value);
	}

	_maliggy_osk_lock_signal(cache->counter_lock, _MALI_OSK_LOCKMODE_RW);

	maliggy_l2_cache_unlock_power_state(cache);

	return MALI_TRUE;
}

u32 maliggy_l2_cache_core_get_counter_src0(struct maliggy_l2_cache_core *cache)
{
	return cache->counter_src0;
}

u32 maliggy_l2_cache_core_get_counter_src1(struct maliggy_l2_cache_core *cache)
{
	return cache->counter_src1;
}

void maliggy_l2_cache_core_get_counter_values(struct maliggy_l2_cache_core *cache, u32 *src0, u32 *value0, u32 *src1, u32 *value1)
{
	MALI_DEBUG_ASSERT(NULL != src0);
	MALI_DEBUG_ASSERT(NULL != value0);
	MALI_DEBUG_ASSERT(NULL != src1);
	MALI_DEBUG_ASSERT(NULL != value1);

	/* Caller must hold the PM lock and know that we are powered on */

	_maliggy_osk_lock_wait(cache->counter_lock, _MALI_OSK_LOCKMODE_RW);

	*src0 = cache->counter_src0;
	*src1 = cache->counter_src1;

	if (cache->counter_src0 != MALI_HW_CORE_NO_COUNTER)
	{
		*value0 = maliggy_hw_core_register_read(&cache->hw_core, MALI400_L2_CACHE_REGISTER_PERFCNT_VAL0);
	}

	if (cache->counter_src1 != MALI_HW_CORE_NO_COUNTER)
	{
		*value1 = maliggy_hw_core_register_read(&cache->hw_core, MALI400_L2_CACHE_REGISTER_PERFCNT_VAL1);
	}

	_maliggy_osk_lock_signal(cache->counter_lock, _MALI_OSK_LOCKMODE_RW);
}

struct maliggy_l2_cache_core *maliggy_l2_cache_core_get_glob_l2_core(u32 index)
{
	if (maliggy_global_num_l2_cache_cores > index)
	{
		return maliggy_global_l2_cache_cores[index];
	}

	return NULL;
}

u32 maliggy_l2_cache_core_get_glob_num_l2_cores(void)
{
	return maliggy_global_num_l2_cache_cores;
}

void maliggy_l2_cache_reset(struct maliggy_l2_cache_core *cache)
{
	/* Invalidate cache (just to keep it in a known state at startup) */
	maliggy_l2_cache_send_command(cache, MALI400_L2_CACHE_REGISTER_COMMAND, MALI400_L2_CACHE_COMMAND_CLEAR_ALL);

	/* Enable cache */
	maliggy_hw_core_register_write(&cache->hw_core, MALI400_L2_CACHE_REGISTER_ENABLE, (u32)MALI400_L2_CACHE_ENABLE_ACCESS | (u32)MALI400_L2_CACHE_ENABLE_READ_ALLOCATE);
	maliggy_hw_core_register_write(&cache->hw_core, MALI400_L2_CACHE_REGISTER_MAX_READS, (u32)maliggy_l2_max_reads);

	/* Restart any performance counters (if enabled) */
	_maliggy_osk_lock_wait(cache->counter_lock, _MALI_OSK_LOCKMODE_RW);

	if (cache->counter_src0 != MALI_HW_CORE_NO_COUNTER)
	{
		maliggy_hw_core_register_write(&cache->hw_core, MALI400_L2_CACHE_REGISTER_PERFCNT_SRC0, cache->counter_src0);
	}

	if (cache->counter_src1 != MALI_HW_CORE_NO_COUNTER)
	{
		maliggy_hw_core_register_write(&cache->hw_core, MALI400_L2_CACHE_REGISTER_PERFCNT_SRC1, cache->counter_src1);
	}

	_maliggy_osk_lock_signal(cache->counter_lock, _MALI_OSK_LOCKMODE_RW);
}

void maliggy_l2_cache_reset_all(void)
{
	int i;
	u32 num_cores = maliggy_l2_cache_core_get_glob_num_l2_cores();

	for (i = 0; i < num_cores; i++)
	{
		maliggy_l2_cache_reset(maliggy_l2_cache_core_get_glob_l2_core(i));
	}
}

void maliggy_l2_cache_invalidate(struct maliggy_l2_cache_core *cache)
{
	MALI_DEBUG_ASSERT_POINTER(cache);

	if (NULL != cache)
	{
		cache->last_invalidated_id = maliggy_scheduler_get_new_id();
		maliggy_l2_cache_send_command(cache, MALI400_L2_CACHE_REGISTER_COMMAND, MALI400_L2_CACHE_COMMAND_CLEAR_ALL);
	}
}

maliggy_bool maliggy_l2_cache_invalidate_conditional(struct maliggy_l2_cache_core *cache, u32 id)
{
	MALI_DEBUG_ASSERT_POINTER(cache);

	if (NULL != cache)
	{
		/* If the last cache invalidation was done by a job with a higher id we
		 * don't have to flush. Since user space will store jobs w/ their
		 * corresponding memory in sequence (first job #0, then job #1, ...),
		 * we don't have to flush for job n-1 if job n has already invalidated
		 * the cache since we know for sure that job n-1's memory was already
		 * written when job n was started. */
		if (((s32)id) <= ((s32)cache->last_invalidated_id))
		{
			return MALI_FALSE;
		}
		else
		{
			cache->last_invalidated_id = maliggy_scheduler_get_new_id();
		}

		maliggy_l2_cache_send_command(cache, MALI400_L2_CACHE_REGISTER_COMMAND, MALI400_L2_CACHE_COMMAND_CLEAR_ALL);
	}
	return MALI_TRUE;
}

void maliggy_l2_cache_invalidate_all(void)
{
	u32 i;
	for (i = 0; i < maliggy_global_num_l2_cache_cores; i++)
	{
		/*additional check*/
		if (MALI_TRUE == maliggy_l2_cache_lock_power_state(maliggy_global_l2_cache_cores[i]))
		{
			_maliggy_osk_errcode_t ret;
			maliggy_global_l2_cache_cores[i]->last_invalidated_id = maliggy_scheduler_get_new_id();
			ret = maliggy_l2_cache_send_command(maliggy_global_l2_cache_cores[i], MALI400_L2_CACHE_REGISTER_COMMAND, MALI400_L2_CACHE_COMMAND_CLEAR_ALL);
			if (_MALI_OSK_ERR_OK != ret)
			{
				MALI_PRINT_ERROR(("Failed to invalidate cache\n"));
			}
		}
		maliggy_l2_cache_unlock_power_state(maliggy_global_l2_cache_cores[i]);
	}
}

void maliggy_l2_cache_invalidate_all_pages(u32 *pages, u32 num_pages)
{
	u32 i;
	for (i = 0; i < maliggy_global_num_l2_cache_cores; i++)
	{
		/*additional check*/
		if (MALI_TRUE == maliggy_l2_cache_lock_power_state(maliggy_global_l2_cache_cores[i]))
		{
			u32 j;
			for (j = 0; j < num_pages; j++)
			{
				_maliggy_osk_errcode_t ret;
				ret = maliggy_l2_cache_send_command(maliggy_global_l2_cache_cores[i], MALI400_L2_CACHE_REGISTER_CLEAR_PAGE, pages[j]);
				if (_MALI_OSK_ERR_OK != ret)
				{
					MALI_PRINT_ERROR(("Failed to invalidate page cache\n"));
				}
			}
		}
		maliggy_l2_cache_unlock_power_state(maliggy_global_l2_cache_cores[i]);
	}
}

maliggy_bool maliggy_l2_cache_lock_power_state(struct maliggy_l2_cache_core *cache)
{
	return maliggy_pm_domain_lock_state(cache->pm_domain);
}

void maliggy_l2_cache_unlock_power_state(struct maliggy_l2_cache_core *cache)
{
	return maliggy_pm_domain_unlock_state(cache->pm_domain);
}

/* -------- local helper functions below -------- */


static _maliggy_osk_errcode_t maliggy_l2_cache_send_command(struct maliggy_l2_cache_core *cache, u32 reg, u32 val)
{
	int i = 0;
	const int loop_count = 100000;

	/*
	 * Grab lock in order to send commands to the L2 cache in a serialized fashion.
	 * The L2 cache will ignore commands if it is busy.
	 */
	_maliggy_osk_lock_wait(cache->command_lock, _MALI_OSK_LOCKMODE_RW);

	/* First, wait for L2 cache command handler to go idle */

	for (i = 0; i < loop_count; i++)
	{
		if (!(maliggy_hw_core_register_read(&cache->hw_core, MALI400_L2_CACHE_REGISTER_STATUS) & (u32)MALI400_L2_CACHE_STATUS_COMMAND_BUSY))
		{
			break;
		}
	}

	if (i == loop_count)
	{
		_maliggy_osk_lock_signal(cache->command_lock, _MALI_OSK_LOCKMODE_RW);
		MALI_DEBUG_PRINT(1, ( "Mali L2 cache: aborting wait for command interface to go idle\n"));
		MALI_ERROR( _MALI_OSK_ERR_FAULT );
	}

	/* then issue the command */
	maliggy_hw_core_register_write(&cache->hw_core, reg, val);

	_maliggy_osk_lock_signal(cache->command_lock, _MALI_OSK_LOCKMODE_RW);

	MALI_SUCCESS;
}
