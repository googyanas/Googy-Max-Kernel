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
#include "mali_session.h"
#include "mali_osk.h"
#include "mali_osk_mali.h"
#include "mali_ukk.h"
#include "mali_kernel_core.h"
#include "mali_memory.h"
#include "mali_mem_validation.h"
#include "mali_mmu.h"
#include "mali_mmu_page_directory.h"
#include "mali_dlbu.h"
#include "mali_broadcast.h"
#include "mali_gp.h"
#include "mali_pp.h"
#include "mali_gp_scheduler.h"
#include "mali_pp_scheduler.h"
#include "mali_group.h"
#include "mali_pm.h"
#include "mali_pmu.h"
#include "mali_scheduler.h"
#include "mali_kernel_utilization.h"
#include "mali_l2_cache.h"
#include "mali_pm_domain.h"
#if defined(CONFIG_MALI400_PROFILING)
#include "mali_osk_profiling.h"
#endif
#if defined(CONFIG_MALI400_INTERNAL_PROFILING)
#include "mali_profiling_internal.h"
#endif


/* Mali GPU memory. Real values come from module parameter or from device specific data */
unsigned int maliggy_dedicated_mem_start = 0;
unsigned int maliggy_dedicated_mem_size = 0;
unsigned int maliggy_shared_mem_size = 0;

/* Frame buffer memory to be accessible by Mali GPU */
int maliggy_fb_start = 0;
int maliggy_fb_size = 0;

/** Start profiling from module load? */
int maliggy_boot_profiling = 0;

/** Limits for the number of PP cores behind each L2 cache. */
int maliggy_max_pp_cores_group_1 = 0xFF;
int maliggy_max_pp_cores_group_2 = 0xFF;

int maliggy_inited_pp_cores_group_1 = 0;
int maliggy_inited_pp_cores_group_2 = 0;

static _maliggy_product_id_t global_product_id = _MALI_PRODUCT_ID_UNKNOWN;
static u32 global_gpu_base_address = 0;
static u32 global_gpu_major_version = 0;
static u32 global_gpu_minor_version = 0;

#define WATCHDOG_MSECS_DEFAULT 4000 /* 4 s */

/* timer related */
int maliggy_max_job_runtime = WATCHDOG_MSECS_DEFAULT;

static _maliggy_osk_errcode_t maliggy_set_global_gpu_base_address(void)
{
	global_gpu_base_address = _maliggy_osk_resource_base_address();
	if (0 == global_gpu_base_address)
	{
		return _MALI_OSK_ERR_ITEM_NOT_FOUND;
	}

	return _MALI_OSK_ERR_OK;
}

static u32 maliggy_get_bcast_id(_maliggy_osk_resource_t *resource_pp)
{
	switch (resource_pp->base - global_gpu_base_address)
	{
	case 0x08000:
	case 0x20000: /* fall-through for aliased mapping */
		return 0x01;
	case 0x0A000:
	case 0x22000: /* fall-through for aliased mapping */
		return 0x02;
	case 0x0C000:
	case 0x24000: /* fall-through for aliased mapping */
		return 0x04;
	case 0x0E000:
	case 0x26000: /* fall-through for aliased mapping */
		return 0x08;
	case 0x28000:
		return 0x10;
	case 0x2A000:
		return 0x20;
	case 0x2C000:
		return 0x40;
	case 0x2E000:
		return 0x80;
	default:
		return 0;
	}
}

static _maliggy_osk_errcode_t maliggy_parse_product_info(void)
{
	/*
	 * Mali-200 has the PP core first, while Mali-300, Mali-400 and Mali-450 have the GP core first.
	 * Look at the version register for the first PP core in order to determine the GPU HW revision.
	 */

	u32 first_pp_offset;
	_maliggy_osk_resource_t first_pp_resource;

	/* Find out where the first PP core is located */
	if (_MALI_OSK_ERR_OK == _maliggy_osk_resource_find(global_gpu_base_address + 0x8000, NULL))
	{
		/* Mali-300/400/450 */
		first_pp_offset = 0x8000;
	}
	else
	{
		/* Mali-200 */
		first_pp_offset = 0x0000;
	}

	/* Find the first PP core resource (again) */
	if (_MALI_OSK_ERR_OK == _maliggy_osk_resource_find(global_gpu_base_address + first_pp_offset, &first_pp_resource))
	{
		/* Create a dummy PP object for this core so that we can read the version register */
		struct maliggy_group *group = maliggy_group_create(NULL, NULL, NULL);
		if (NULL != group)
		{
			struct maliggy_pp_core *pp_core = maliggy_pp_create(&first_pp_resource, group, MALI_FALSE, maliggy_get_bcast_id(&first_pp_resource));
			if (NULL != pp_core)
			{
				u32 pp_version = maliggy_pp_core_get_version(pp_core);
				maliggy_group_delete(group);

				global_gpu_major_version = (pp_version >> 8) & 0xFF;
				global_gpu_minor_version = pp_version & 0xFF;

				switch (pp_version >> 16)
				{
					case MALI200_PP_PRODUCT_ID:
						global_product_id = _MALI_PRODUCT_ID_MALI200;
						MALI_DEBUG_PRINT(2, ("Found Mali GPU Mali-200 r%up%u\n", global_gpu_major_version, global_gpu_minor_version));
						MALI_PRINT_ERROR(("Mali-200 is not supported by this driver.\n"));
						_maliggy_osk_abort();
						break;
					case MALI300_PP_PRODUCT_ID:
						global_product_id = _MALI_PRODUCT_ID_MALI300;
						MALI_DEBUG_PRINT(2, ("Found Mali GPU Mali-300 r%up%u\n", global_gpu_major_version, global_gpu_minor_version));
						break;
					case MALI400_PP_PRODUCT_ID:
						global_product_id = _MALI_PRODUCT_ID_MALI400;
						MALI_DEBUG_PRINT(2, ("Found Mali GPU Mali-400 MP r%up%u\n", global_gpu_major_version, global_gpu_minor_version));
						break;
					case MALI450_PP_PRODUCT_ID:
						global_product_id = _MALI_PRODUCT_ID_MALI450;
						MALI_DEBUG_PRINT(2, ("Found Mali GPU Mali-450 MP r%up%u\n", global_gpu_major_version, global_gpu_minor_version));
						break;
					default:
						MALI_DEBUG_PRINT(2, ("Found unknown Mali GPU (r%up%u)\n", global_gpu_major_version, global_gpu_minor_version));
						return _MALI_OSK_ERR_FAULT;
				}

				return _MALI_OSK_ERR_OK;
			}
			else
			{
				MALI_PRINT_ERROR(("Failed to create initial PP object\n"));
			}
		}
		else
		{
			MALI_PRINT_ERROR(("Failed to create initial group object\n"));
		}
	}
	else
	{
		MALI_PRINT_ERROR(("First PP core not specified in config file\n"));
	}

	return _MALI_OSK_ERR_FAULT;
}


void maliggy_resource_count(u32 *pp_count, u32 *l2_count)
{
	*pp_count = 0;
	*l2_count = 0;

	if (_MALI_OSK_ERR_OK == _maliggy_osk_resource_find(global_gpu_base_address + 0x08000, NULL))
	{
		++(*pp_count);
	}
	if (_MALI_OSK_ERR_OK == _maliggy_osk_resource_find(global_gpu_base_address + 0x0A000, NULL))
	{
		++(*pp_count);
	}
	if (_MALI_OSK_ERR_OK == _maliggy_osk_resource_find(global_gpu_base_address + 0x0C000, NULL))
	{
		++(*pp_count);
	}
	if (_MALI_OSK_ERR_OK == _maliggy_osk_resource_find(global_gpu_base_address + 0x0E000, NULL))
	{
		++(*pp_count);
	}
	if (_MALI_OSK_ERR_OK == _maliggy_osk_resource_find(global_gpu_base_address + 0x28000, NULL))
	{
		++(*pp_count);
	}
	if (_MALI_OSK_ERR_OK == _maliggy_osk_resource_find(global_gpu_base_address + 0x2A000, NULL))
	{
		++(*pp_count);
	}
	if (_MALI_OSK_ERR_OK == _maliggy_osk_resource_find(global_gpu_base_address + 0x2C000, NULL))
	{
		++(*pp_count);
	}
	if (_MALI_OSK_ERR_OK == _maliggy_osk_resource_find(global_gpu_base_address + 0x2E000, NULL))
	{
		++(*pp_count);
	}

	if (_MALI_OSK_ERR_OK == _maliggy_osk_resource_find(global_gpu_base_address + 0x1000, NULL))
	{
		++(*l2_count);
	}
	if (_MALI_OSK_ERR_OK == _maliggy_osk_resource_find(global_gpu_base_address + 0x10000, NULL))
	{
		++(*l2_count);
	}
	if (_MALI_OSK_ERR_OK == _maliggy_osk_resource_find(global_gpu_base_address + 0x11000, NULL))
	{
		++(*l2_count);
	}
}

static void maliggy_delete_groups(void)
{
	while (0 < maliggy_group_get_glob_num_groups())
	{
		maliggy_group_delete(maliggy_group_get_glob_group(0));
	}
}

static void maliggy_delete_l2_cache_cores(void)
{
	while (0 < maliggy_l2_cache_core_get_glob_num_l2_cores())
	{
		maliggy_l2_cache_delete(maliggy_l2_cache_core_get_glob_l2_core(0));
	}
}

static struct maliggy_l2_cache_core *maliggy_create_l2_cache_core(_maliggy_osk_resource_t *resource)
{
	struct maliggy_l2_cache_core *l2_cache = NULL;

	if (NULL != resource)
	{

		MALI_DEBUG_PRINT(3, ("Found L2 cache %s\n", resource->description));

		l2_cache = maliggy_l2_cache_create(resource);
		if (NULL == l2_cache)
		{
			MALI_PRINT_ERROR(("Failed to create L2 cache object\n"));
			return NULL;
		}
	}
	MALI_DEBUG_PRINT(3, ("Created L2 cache core object\n"));

	return l2_cache;
}

static _maliggy_osk_errcode_t maliggy_parse_config_l2_cache(void)
{
	struct maliggy_l2_cache_core *l2_cache = NULL;

	if (maliggy_is_maliggy400())
	{
		_maliggy_osk_resource_t l2_resource;
		if (_MALI_OSK_ERR_OK != _maliggy_osk_resource_find(global_gpu_base_address + 0x1000, &l2_resource))
		{
			MALI_DEBUG_PRINT(3, ("Did not find required Mali L2 cache in config file\n"));
			return _MALI_OSK_ERR_FAULT;
		}

		l2_cache = maliggy_create_l2_cache_core(&l2_resource);
		if (NULL == l2_cache)
		{
			return _MALI_OSK_ERR_FAULT;
		}
	}
	else if (maliggy_is_maliggy450())
	{
		/*
		 * L2 for GP    at 0x10000
		 * L2 for PP0-3 at 0x01000
		 * L2 for PP4-7 at 0x11000 (optional)
		 */

		_maliggy_osk_resource_t l2_gp_resource;
		_maliggy_osk_resource_t l2_pp_grp0_resource;
		_maliggy_osk_resource_t l2_pp_grp1_resource;

		/* Make cluster for GP's L2 */
		if (_MALI_OSK_ERR_OK == _maliggy_osk_resource_find(global_gpu_base_address + 0x10000, &l2_gp_resource))
		{
			MALI_DEBUG_PRINT(3, ("Creating Mali-450 L2 cache core for GP\n"));
			l2_cache = maliggy_create_l2_cache_core(&l2_gp_resource);
			if (NULL == l2_cache)
			{
				return _MALI_OSK_ERR_FAULT;
			}
		}
		else
		{
			MALI_DEBUG_PRINT(3, ("Did not find required Mali L2 cache for GP in config file\n"));
			return _MALI_OSK_ERR_FAULT;
		}

		/* Make cluster for first PP core group */
		if (_MALI_OSK_ERR_OK == _maliggy_osk_resource_find(global_gpu_base_address + 0x1000, &l2_pp_grp0_resource))
		{
			MALI_DEBUG_PRINT(3, ("Creating Mali-450 L2 cache core for PP group 0\n"));
			l2_cache = maliggy_create_l2_cache_core(&l2_pp_grp0_resource);
			if (NULL == l2_cache)
			{
				return _MALI_OSK_ERR_FAULT;
			}
			maliggy_pm_domain_add_l2(MALI_PMU_M450_DOM1, l2_cache);
		}
		else
		{
			MALI_DEBUG_PRINT(3, ("Did not find required Mali L2 cache for PP group 0 in config file\n"));
			return _MALI_OSK_ERR_FAULT;
		}

		/* Second PP core group is optional, don't fail if we don't find it */
		if (_MALI_OSK_ERR_OK == _maliggy_osk_resource_find(global_gpu_base_address + 0x11000, &l2_pp_grp1_resource))
		{
			MALI_DEBUG_PRINT(3, ("Creating Mali-450 L2 cache core for PP group 1\n"));
			l2_cache = maliggy_create_l2_cache_core(&l2_pp_grp1_resource);
			if (NULL == l2_cache)
			{
				return _MALI_OSK_ERR_FAULT;
			}
			maliggy_pm_domain_add_l2(MALI_PMU_M450_DOM3, l2_cache);
		}
	}

	return _MALI_OSK_ERR_OK;
}

static struct maliggy_group *maliggy_create_group(struct maliggy_l2_cache_core *cache,
                                             _maliggy_osk_resource_t *resource_mmu,
                                             _maliggy_osk_resource_t *resource_gp,
                                             _maliggy_osk_resource_t *resource_pp)
{
	struct maliggy_mmu_core *mmu;
	struct maliggy_group *group;

	MALI_DEBUG_PRINT(3, ("Starting new group for MMU %s\n", resource_mmu->description));

	/* Create the group object */
	group = maliggy_group_create(cache, NULL, NULL);
	if (NULL == group)
	{
		MALI_PRINT_ERROR(("Failed to create group object for MMU %s\n", resource_mmu->description));
		return NULL;
	}

	/* Create the MMU object inside group */
	mmu = maliggy_mmu_create(resource_mmu, group, MALI_FALSE);
	if (NULL == mmu)
	{
		MALI_PRINT_ERROR(("Failed to create MMU object\n"));
		maliggy_group_delete(group);
		return NULL;
	}

	if (NULL != resource_gp)
	{
		/* Create the GP core object inside this group */
		struct maliggy_gp_core *gp_core = maliggy_gp_create(resource_gp, group);
		if (NULL == gp_core)
		{
			/* No need to clean up now, as we will clean up everything linked in from the cluster when we fail this function */
			MALI_PRINT_ERROR(("Failed to create GP object\n"));
			maliggy_group_delete(group);
			return NULL;
		}
	}

	if (NULL != resource_pp)
	{
		struct maliggy_pp_core *pp_core;

		/* Create the PP core object inside this group */
		pp_core = maliggy_pp_create(resource_pp, group, MALI_FALSE, maliggy_get_bcast_id(resource_pp));
		if (NULL == pp_core)
		{
			/* No need to clean up now, as we will clean up everything linked in from the cluster when we fail this function */
			MALI_PRINT_ERROR(("Failed to create PP object\n"));
			maliggy_group_delete(group);
			return NULL;
		}
	}

	/* Reset group */
	maliggy_group_lock(group);
	maliggy_group_reset(group);
	maliggy_group_unlock(group);

	return group;
}

static _maliggy_osk_errcode_t maliggy_create_virtual_group(_maliggy_osk_resource_t *resource_mmu_pp_bcast,
                                                    _maliggy_osk_resource_t *resource_pp_bcast,
                                                    _maliggy_osk_resource_t *resource_dlbu,
                                                    _maliggy_osk_resource_t *resource_bcast)
{
	struct maliggy_mmu_core *mmu_pp_bcast_core;
	struct maliggy_pp_core *pp_bcast_core;
	struct maliggy_dlbu_core *dlbu_core;
	struct maliggy_bcast_unit *bcast_core;
	struct maliggy_group *group;

	MALI_DEBUG_PRINT(2, ("Starting new virtual group for MMU PP broadcast core %s\n", resource_mmu_pp_bcast->description));

	/* Create the DLBU core object */
	dlbu_core = maliggy_dlbu_create(resource_dlbu);
	if (NULL == dlbu_core)
	{
		MALI_PRINT_ERROR(("Failed to create DLBU object \n"));
		return _MALI_OSK_ERR_FAULT;
	}

	/* Create the Broadcast unit core */
	bcast_core = maliggy_bcast_unit_create(resource_bcast);
	if (NULL == bcast_core)
	{
		MALI_PRINT_ERROR(("Failed to create Broadcast unit object!\n"));
		maliggy_dlbu_delete(dlbu_core);
		return _MALI_OSK_ERR_FAULT;
	}

	/* Create the group object */
	group = maliggy_group_create(NULL, dlbu_core, bcast_core);
	if (NULL == group)
	{
		MALI_PRINT_ERROR(("Failed to create group object for MMU PP broadcast core %s\n", resource_mmu_pp_bcast->description));
		maliggy_bcast_unit_delete(bcast_core);
		maliggy_dlbu_delete(dlbu_core);
		return _MALI_OSK_ERR_FAULT;
	}

	/* Create the MMU object inside group */
	mmu_pp_bcast_core = maliggy_mmu_create(resource_mmu_pp_bcast, group, MALI_TRUE);
	if (NULL == mmu_pp_bcast_core)
	{
		MALI_PRINT_ERROR(("Failed to create MMU PP broadcast object\n"));
		maliggy_group_delete(group);
		return _MALI_OSK_ERR_FAULT;
	}

	/* Create the PP core object inside this group */
	pp_bcast_core = maliggy_pp_create(resource_pp_bcast, group, MALI_TRUE, 0);
	if (NULL == pp_bcast_core)
	{
		/* No need to clean up now, as we will clean up everything linked in from the cluster when we fail this function */
		MALI_PRINT_ERROR(("Failed to create PP object\n"));
		maliggy_group_delete(group);
		return _MALI_OSK_ERR_FAULT;
	}

	return _MALI_OSK_ERR_OK;
}

static _maliggy_osk_errcode_t maliggy_parse_config_groups(void)
{
	struct maliggy_group *group;
	int cluster_id_gp = 0;
	int cluster_id_pp_grp0 = 0;
	int cluster_id_pp_grp1 = 0;
	int i;

	_maliggy_osk_resource_t resource_gp;
	_maliggy_osk_resource_t resource_gp_mmu;
	_maliggy_osk_resource_t resource_pp[8];
	_maliggy_osk_resource_t resource_pp_mmu[8];
	_maliggy_osk_resource_t resource_pp_mmu_bcast;
	_maliggy_osk_resource_t resource_pp_bcast;
	_maliggy_osk_resource_t resource_dlbu;
	_maliggy_osk_resource_t resource_bcast;
	_maliggy_osk_errcode_t resource_gp_found;
	_maliggy_osk_errcode_t resource_gp_mmu_found;
	_maliggy_osk_errcode_t resource_pp_found[8];
	_maliggy_osk_errcode_t resource_pp_mmu_found[8];
	_maliggy_osk_errcode_t resource_pp_mmu_bcast_found;
	_maliggy_osk_errcode_t resource_pp_bcast_found;
	_maliggy_osk_errcode_t resource_dlbu_found;
	_maliggy_osk_errcode_t resource_bcast_found;

	if (!(maliggy_is_maliggy400() || maliggy_is_maliggy450()))
	{
		/* No known HW core */
		return _MALI_OSK_ERR_FAULT;
	}

	if (maliggy_is_maliggy450())
	{
		/* Mali-450 have separate L2s for GP, and PP core group(s) */
		cluster_id_pp_grp0 = 1;
		cluster_id_pp_grp1 = 2;
	}

	resource_gp_found = _maliggy_osk_resource_find(global_gpu_base_address + 0x00000, &resource_gp);
	resource_gp_mmu_found = _maliggy_osk_resource_find(global_gpu_base_address + 0x03000, &resource_gp_mmu);
	resource_pp_found[0] = _maliggy_osk_resource_find(global_gpu_base_address + 0x08000, &(resource_pp[0]));
	resource_pp_found[1] = _maliggy_osk_resource_find(global_gpu_base_address + 0x0A000, &(resource_pp[1]));
	resource_pp_found[2] = _maliggy_osk_resource_find(global_gpu_base_address + 0x0C000, &(resource_pp[2]));
	resource_pp_found[3] = _maliggy_osk_resource_find(global_gpu_base_address + 0x0E000, &(resource_pp[3]));
	resource_pp_found[4] = _maliggy_osk_resource_find(global_gpu_base_address + 0x28000, &(resource_pp[4]));
	resource_pp_found[5] = _maliggy_osk_resource_find(global_gpu_base_address + 0x2A000, &(resource_pp[5]));
	resource_pp_found[6] = _maliggy_osk_resource_find(global_gpu_base_address + 0x2C000, &(resource_pp[6]));
	resource_pp_found[7] = _maliggy_osk_resource_find(global_gpu_base_address + 0x2E000, &(resource_pp[7]));
	resource_pp_mmu_found[0] = _maliggy_osk_resource_find(global_gpu_base_address + 0x04000, &(resource_pp_mmu[0]));
	resource_pp_mmu_found[1] = _maliggy_osk_resource_find(global_gpu_base_address + 0x05000, &(resource_pp_mmu[1]));
	resource_pp_mmu_found[2] = _maliggy_osk_resource_find(global_gpu_base_address + 0x06000, &(resource_pp_mmu[2]));
	resource_pp_mmu_found[3] = _maliggy_osk_resource_find(global_gpu_base_address + 0x07000, &(resource_pp_mmu[3]));
	resource_pp_mmu_found[4] = _maliggy_osk_resource_find(global_gpu_base_address + 0x1C000, &(resource_pp_mmu[4]));
	resource_pp_mmu_found[5] = _maliggy_osk_resource_find(global_gpu_base_address + 0x1D000, &(resource_pp_mmu[5]));
	resource_pp_mmu_found[6] = _maliggy_osk_resource_find(global_gpu_base_address + 0x1E000, &(resource_pp_mmu[6]));
	resource_pp_mmu_found[7] = _maliggy_osk_resource_find(global_gpu_base_address + 0x1F000, &(resource_pp_mmu[7]));


	if (maliggy_is_maliggy450())
	{
		resource_bcast_found = _maliggy_osk_resource_find(global_gpu_base_address + 0x13000, &resource_bcast);
		resource_dlbu_found = _maliggy_osk_resource_find(global_gpu_base_address + 0x14000, &resource_dlbu);
		resource_pp_mmu_bcast_found = _maliggy_osk_resource_find(global_gpu_base_address + 0x15000, &resource_pp_mmu_bcast);
		resource_pp_bcast_found = _maliggy_osk_resource_find(global_gpu_base_address + 0x16000, &resource_pp_bcast);

		if (_MALI_OSK_ERR_OK != resource_bcast_found ||
		    _MALI_OSK_ERR_OK != resource_dlbu_found ||
		    _MALI_OSK_ERR_OK != resource_pp_mmu_bcast_found ||
		    _MALI_OSK_ERR_OK != resource_pp_bcast_found)
		{
			/* Missing mandatory core(s) for Mali-450 */
			MALI_DEBUG_PRINT(2, ("Missing mandatory resources, Mali-450 needs DLBU, Broadcast unit, virtual PP core and virtual MMU\n"));
			return _MALI_OSK_ERR_FAULT;
		}
	}

	if (_MALI_OSK_ERR_OK != resource_gp_found ||
	    _MALI_OSK_ERR_OK != resource_gp_mmu_found ||
	    _MALI_OSK_ERR_OK != resource_pp_found[0] ||
	    _MALI_OSK_ERR_OK != resource_pp_mmu_found[0])
	{
		/* Missing mandatory core(s) */
		MALI_DEBUG_PRINT(2, ("Missing mandatory resource, need at least one GP and one PP, both with a separate MMU\n"));
		return _MALI_OSK_ERR_FAULT;
	}

	MALI_DEBUG_ASSERT(1 <= maliggy_l2_cache_core_get_glob_num_l2_cores());
	group = maliggy_create_group(maliggy_l2_cache_core_get_glob_l2_core(cluster_id_gp), &resource_gp_mmu, &resource_gp, NULL);
	if (NULL == group)
	{
		return _MALI_OSK_ERR_FAULT;
	}

	/* Create group for first (and mandatory) PP core */
	MALI_DEBUG_ASSERT(maliggy_l2_cache_core_get_glob_num_l2_cores() >= (cluster_id_pp_grp0 + 1)); /* >= 1 on Mali-300 and Mali-400, >= 2 on Mali-450 */
	group = maliggy_create_group(maliggy_l2_cache_core_get_glob_l2_core(cluster_id_pp_grp0), &resource_pp_mmu[0], NULL, &resource_pp[0]);
	if (NULL == group)
	{
		return _MALI_OSK_ERR_FAULT;
	}
	if (maliggy_is_maliggy450())
	{
		maliggy_pm_domain_add_group(MALI_PMU_M450_DOM1, group);
	}
	else
	{
		maliggy_pm_domain_add_group(MALI_PMU_M400_PP0, group);
	}
	maliggy_inited_pp_cores_group_1++;

	/* Create groups for rest of the cores in the first PP core group */
	for (i = 1; i < 4; i++) /* First half of the PP cores belong to first core group */
	{
		if (maliggy_inited_pp_cores_group_1 < maliggy_max_pp_cores_group_1)
		{
			if (_MALI_OSK_ERR_OK == resource_pp_found[i] && _MALI_OSK_ERR_OK == resource_pp_mmu_found[i])
			{
				group = maliggy_create_group(maliggy_l2_cache_core_get_glob_l2_core(cluster_id_pp_grp0), &resource_pp_mmu[i], NULL, &resource_pp[i]);
				if (NULL == group)
				{
					return _MALI_OSK_ERR_FAULT;
				}
				if (maliggy_is_maliggy450())
				{
					maliggy_pm_domain_add_group(MALI_PMU_M450_DOM2, group);
				}
				else
				{
					maliggy_pm_domain_add_group(MALI_PMU_M400_PP0 + i, group);
				}
				maliggy_inited_pp_cores_group_1++;
			}
		}
	}

	/* Create groups for cores in the second PP core group */
	for (i = 4; i < 8; i++) /* Second half of the PP cores belong to second core group */
	{
		if (maliggy_inited_pp_cores_group_2 < maliggy_max_pp_cores_group_2)
		{
			if (_MALI_OSK_ERR_OK == resource_pp_found[i] && _MALI_OSK_ERR_OK == resource_pp_mmu_found[i])
			{
				MALI_DEBUG_ASSERT(maliggy_l2_cache_core_get_glob_num_l2_cores() >= 2); /* Only Mali-450 have a second core group */
				group = maliggy_create_group(maliggy_l2_cache_core_get_glob_l2_core(cluster_id_pp_grp1), &resource_pp_mmu[i], NULL, &resource_pp[i]);
				if (NULL == group)
				{
					return _MALI_OSK_ERR_FAULT;
				}
				maliggy_pm_domain_add_group(MALI_PMU_M450_DOM3, group);
				maliggy_inited_pp_cores_group_2++;
			}
		}
	}

	if(maliggy_is_maliggy450())
	{
		_maliggy_osk_errcode_t err = maliggy_create_virtual_group(&resource_pp_mmu_bcast, &resource_pp_bcast, &resource_dlbu, &resource_bcast);
		if (_MALI_OSK_ERR_OK != err)
		{
			return err;
		}
	}

	maliggy_max_pp_cores_group_1 = maliggy_inited_pp_cores_group_1;
	maliggy_max_pp_cores_group_2 = maliggy_inited_pp_cores_group_2;
	MALI_DEBUG_PRINT(2, ("%d+%d PP cores initialized\n", maliggy_inited_pp_cores_group_1, maliggy_inited_pp_cores_group_2));

	return _MALI_OSK_ERR_OK;
}

static _maliggy_osk_errcode_t maliggy_check_shared_interrupts(void)
{
#if !defined(CONFIG_MALI_SHARED_INTERRUPTS)
	if (MALI_TRUE == _maliggy_osk_shared_interrupts())
	{
		MALI_PRINT_ERROR(("Shared interrupts detected, but driver support is not enabled\n"));
		return _MALI_OSK_ERR_FAULT;
	}
#endif /* !defined(CONFIG_MALI_SHARED_INTERRUPTS) */

	/* It is OK to compile support for shared interrupts even if Mali is not using it. */
	return _MALI_OSK_ERR_OK;
}

static _maliggy_osk_errcode_t maliggy_create_pm_domains(void)
{
	struct maliggy_pm_domain *domain;
	u32 number_of_pp_cores = 0;
	u32 number_of_l2_caches = 0;

	maliggy_resource_count(&number_of_pp_cores, &number_of_l2_caches);

	if (maliggy_is_maliggy450())
	{
		MALI_DEBUG_PRINT(2, ("Creating PM domains for Mali-450 MP%d\n", number_of_pp_cores));
		switch (number_of_pp_cores)
		{
			case 8: /* Fall through */
			case 6: /* Fall through */
				domain = maliggy_pm_domain_create(MALI_PMU_M450_DOM3, MALI_PMU_M450_DOM3_MASK);
				MALI_CHECK(NULL != domain, _MALI_OSK_ERR_NOMEM);
			case 4: /* Fall through */
			case 3: /* Fall through */
			case 2: /* Fall through */
				domain = maliggy_pm_domain_create(MALI_PMU_M450_DOM2, MALI_PMU_M450_DOM2_MASK);
				MALI_CHECK(NULL != domain, _MALI_OSK_ERR_NOMEM);
				domain = maliggy_pm_domain_create(MALI_PMU_M450_DOM1, MALI_PMU_M450_DOM1_MASK);
				MALI_CHECK(NULL != domain, _MALI_OSK_ERR_NOMEM);

				break;
			default:
				MALI_PRINT_ERROR(("Unsupported core configuration\n"));
				MALI_DEBUG_ASSERT(0);
		}
	}
	else
	{
		int i;
		u32 mask = MALI_PMU_M400_PP0_MASK;

		MALI_DEBUG_PRINT(2, ("Creating PM domains for Mali-400 MP%d\n", number_of_pp_cores));

		MALI_DEBUG_ASSERT(maliggy_is_maliggy400());

		for (i = 0; i < number_of_pp_cores; i++)
		{
			MALI_CHECK(NULL != maliggy_pm_domain_create(i, mask), _MALI_OSK_ERR_NOMEM);

			/* Shift mask up, for next core */
			mask = mask << 1;
		}
	}
	return _MALI_OSK_ERR_OK;
}

static _maliggy_osk_errcode_t maliggy_parse_config_pmu(void)
{
	_maliggy_osk_resource_t resource_pmu;

	MALI_DEBUG_ASSERT(0 != global_gpu_base_address);

	if (_MALI_OSK_ERR_OK == _maliggy_osk_resource_find(global_gpu_base_address + 0x02000, &resource_pmu))
	{
		struct maliggy_pmu_core *pmu;
		u32 number_of_pp_cores = 0;
		u32 number_of_l2_caches = 0;

		maliggy_resource_count(&number_of_pp_cores, &number_of_l2_caches);

		pmu = maliggy_pmu_create(&resource_pmu, number_of_pp_cores, number_of_l2_caches);
		if (NULL == pmu)
		{
			MALI_PRINT_ERROR(("Failed to create PMU\n"));
			return _MALI_OSK_ERR_FAULT;
		}
	}

	/* It's ok if the PMU doesn't exist */
	return _MALI_OSK_ERR_OK;
}

static _maliggy_osk_errcode_t maliggy_parse_config_memory(void)
{
	_maliggy_osk_errcode_t ret;

	if (0 == maliggy_dedicated_mem_start && 0 == maliggy_dedicated_mem_size && 0 == maliggy_shared_mem_size)
	{
		/* Memory settings are not overridden by module parameters, so use device settings */
		struct _maliggy_osk_device_data data = { 0, };

		if (_MALI_OSK_ERR_OK == _maliggy_osk_device_data_get(&data))
		{
			/* Use device specific settings (if defined) */
			maliggy_dedicated_mem_start = data.dedicated_mem_start;
			maliggy_dedicated_mem_size = data.dedicated_mem_size;
			maliggy_shared_mem_size = data.shared_mem_size;
		}

		if (0 == maliggy_dedicated_mem_start && 0 == maliggy_dedicated_mem_size && 0 == maliggy_shared_mem_size)
		{
			/* No GPU memory specified */
			return _MALI_OSK_ERR_INVALID_ARGS;
		}

		MALI_DEBUG_PRINT(2, ("Using device defined memory settings (dedicated: 0x%08X@0x%08X, shared: 0x%08X)\n",
		                     maliggy_dedicated_mem_size, maliggy_dedicated_mem_start, maliggy_shared_mem_size));
	}
	else
	{
		MALI_DEBUG_PRINT(2, ("Using module defined memory settings (dedicated: 0x%08X@0x%08X, shared: 0x%08X)\n",
		                     maliggy_dedicated_mem_size, maliggy_dedicated_mem_start, maliggy_shared_mem_size));
	}

	if (0 < maliggy_dedicated_mem_size && 0 != maliggy_dedicated_mem_start)
	{
		/* Dedicated memory */
		ret = maliggy_memory_core_resource_dedicated_memory(maliggy_dedicated_mem_start, maliggy_dedicated_mem_size);
		if (_MALI_OSK_ERR_OK != ret)
		{
			MALI_PRINT_ERROR(("Failed to register dedicated memory\n"));
			maliggy_memory_terminate();
			return ret;
		}
	}

	if (0 < maliggy_shared_mem_size)
	{
		/* Shared OS memory */
		ret = maliggy_memory_core_resource_os_memory(maliggy_shared_mem_size);
		if (_MALI_OSK_ERR_OK != ret)
		{
			MALI_PRINT_ERROR(("Failed to register shared OS memory\n"));
			maliggy_memory_terminate();
			return ret;
		}
	}

	if (0 == maliggy_fb_start && 0 == maliggy_fb_size)
	{
		/* Frame buffer settings are not overridden by module parameters, so use device settings */
		struct _maliggy_osk_device_data data = { 0, };

		if (_MALI_OSK_ERR_OK == _maliggy_osk_device_data_get(&data))
		{
			/* Use device specific settings (if defined) */
			maliggy_fb_start = data.fb_start;
			maliggy_fb_size = data.fb_size;
		}

		MALI_DEBUG_PRINT(2, ("Using device defined frame buffer settings (0x%08X@0x%08X)\n",
		                     maliggy_fb_size, maliggy_fb_start));
	}
	else
	{
		MALI_DEBUG_PRINT(2, ("Using module defined frame buffer settings (0x%08X@0x%08X)\n",
		                     maliggy_fb_size, maliggy_fb_start));
	}

	if (0 != maliggy_fb_size)
	{
		/* Register frame buffer */
		ret = maliggy_mem_validation_add_range(maliggy_fb_start, maliggy_fb_size);
		if (_MALI_OSK_ERR_OK != ret)
		{
			MALI_PRINT_ERROR(("Failed to register frame buffer memory region\n"));
			maliggy_memory_terminate();
			return ret;
		}
	}

	return _MALI_OSK_ERR_OK;
}

_maliggy_osk_errcode_t maliggy_initialize_subsystems(void)
{
	_maliggy_osk_errcode_t err;
	struct maliggy_pmu_core *pmu;

	err = maliggy_session_initialize();
	if (_MALI_OSK_ERR_OK != err) goto session_init_failed;

#if defined(CONFIG_MALI400_PROFILING)
	err = _maliggy_osk_profiling_init(maliggy_boot_profiling ? MALI_TRUE : MALI_FALSE);
	if (_MALI_OSK_ERR_OK != err)
	{
		/* No biggie if we weren't able to initialize the profiling */
		MALI_PRINT_ERROR(("Failed to initialize profiling, feature will be unavailable\n"));
	}
#endif

	err = maliggy_memory_initialize();
	if (_MALI_OSK_ERR_OK != err) goto memory_init_failed;

	/* Configure memory early. Memory allocation needed for maliggy_mmu_initialize. */
	err = maliggy_parse_config_memory();
	if (_MALI_OSK_ERR_OK != err) goto parse_memory_config_failed;

	err = maliggy_set_global_gpu_base_address();
	if (_MALI_OSK_ERR_OK != err) goto set_global_gpu_base_address_failed;

	err = maliggy_check_shared_interrupts();
	if (_MALI_OSK_ERR_OK != err) goto check_shared_interrupts_failed;

	err = maliggy_pp_scheduler_initialize();
	if (_MALI_OSK_ERR_OK != err) goto pp_scheduler_init_failed;

	/* Initialize the power management module */
	err = maliggy_pm_initialize();
	if (_MALI_OSK_ERR_OK != err) goto pm_init_failed;

	/* Initialize the MALI PMU */
	err = maliggy_parse_config_pmu();
	if (_MALI_OSK_ERR_OK != err) goto parse_pmu_config_failed;

	/* Make sure the power stays on for the rest of this function */
	err = _maliggy_osk_pm_dev_ref_add();
	if (_MALI_OSK_ERR_OK != err) goto pm_always_on_failed;

	/*
	 * If run-time PM is used, then the maliggy_pm module has now already been
	 * notified that the power now is on (through the resume callback functions).
	 * However, if run-time PM is not used, then there will probably not be any
	 * calls to the resume callback functions, so we need to explicitly tell it
	 * that the power is on.
	 */
	maliggy_pm_set_power_is_on();

	/* Reset PMU HW and ensure all Mali power domains are on */
	pmu = maliggy_pmu_get_global_pmu_core();
	if (NULL != pmu)
	{
		err = maliggy_pmu_reset(pmu);
		if (_MALI_OSK_ERR_OK != err) goto pmu_reset_failed;
	}

	/* Detect which Mali GPU we are dealing with */
	err = maliggy_parse_product_info();
	if (_MALI_OSK_ERR_OK != err) goto product_info_parsing_failed;

	/* The global_product_id is now populated with the correct Mali GPU */

	/* Create PM domains only if PMU exists */
	if (NULL != pmu)
	{
		err = maliggy_create_pm_domains();
		if (_MALI_OSK_ERR_OK != err) goto pm_domain_failed;
	}

	/* Initialize MMU module */
	err = maliggy_mmu_initialize();
	if (_MALI_OSK_ERR_OK != err) goto mmu_init_failed;

	if (maliggy_is_maliggy450())
	{
		err = maliggy_dlbu_initialize();
		if (_MALI_OSK_ERR_OK != err) goto dlbu_init_failed;
	}

	/* Start configuring the actual Mali hardware. */
	err = maliggy_parse_config_l2_cache();
	if (_MALI_OSK_ERR_OK != err) goto config_parsing_failed;
	err = maliggy_parse_config_groups();
	if (_MALI_OSK_ERR_OK != err) goto config_parsing_failed;

	/* Initialize the schedulers */
	err = maliggy_scheduler_initialize();
	if (_MALI_OSK_ERR_OK != err) goto scheduler_init_failed;
	err = maliggy_gp_scheduler_initialize();
	if (_MALI_OSK_ERR_OK != err) goto gp_scheduler_init_failed;

	/* PP scheduler population can't fail */
	maliggy_pp_scheduler_populate();

	/* Initialize the GPU utilization tracking */
	err = maliggy_utilization_init();
	if (_MALI_OSK_ERR_OK != err) goto utilization_init_failed;

	/* Allowing the system to be turned off */
	_maliggy_osk_pm_dev_ref_dec();

	MALI_SUCCESS; /* all ok */

	/* Error handling */

utilization_init_failed:
	maliggy_pp_scheduler_depopulate();
	maliggy_gp_scheduler_terminate();
gp_scheduler_init_failed:
	maliggy_scheduler_terminate();
scheduler_init_failed:
config_parsing_failed:
	maliggy_delete_groups(); /* Delete any groups not (yet) owned by a scheduler */
	maliggy_delete_l2_cache_cores(); /* Delete L2 cache cores even if config parsing failed. */
dlbu_init_failed:
	maliggy_dlbu_terminate();
mmu_init_failed:
	maliggy_pm_domain_terminate();
pm_domain_failed:
	/* Nothing to roll back */
product_info_parsing_failed:
	/* Nothing to roll back */
pmu_reset_failed:
	/* Allowing the system to be turned off */
	_maliggy_osk_pm_dev_ref_dec();
pm_always_on_failed:
	pmu = maliggy_pmu_get_global_pmu_core();
	if (NULL != pmu)
	{
		maliggy_pmu_delete(pmu);
	}
parse_pmu_config_failed:
	maliggy_pm_terminate();
pm_init_failed:
	maliggy_pp_scheduler_terminate();
pp_scheduler_init_failed:
check_shared_interrupts_failed:
	global_gpu_base_address = 0;
set_global_gpu_base_address_failed:
	global_gpu_base_address = 0;
parse_memory_config_failed:
	maliggy_memory_terminate();
memory_init_failed:
#if defined(CONFIG_MALI400_PROFILING)
	_maliggy_osk_profiling_term();
#endif
	maliggy_session_terminate();
session_init_failed:
	return err;
}

void maliggy_terminate_subsystems(void)
{
	struct maliggy_pmu_core *pmu = maliggy_pmu_get_global_pmu_core();

	MALI_DEBUG_PRINT(2, ("terminate_subsystems() called\n"));

	/* shut down subsystems in reverse order from startup */

	/* We need the GPU to be powered up for the terminate sequence */
	_maliggy_osk_pm_dev_ref_add();

	maliggy_utilization_term();
	maliggy_pp_scheduler_depopulate();
	maliggy_gp_scheduler_terminate();
	maliggy_scheduler_terminate();
	maliggy_delete_l2_cache_cores();
	if (maliggy_is_maliggy450())
	{
		maliggy_dlbu_terminate();
	}
	maliggy_mmu_terminate();
	if (NULL != pmu)
	{
		maliggy_pmu_delete(pmu);
	}
	maliggy_pm_terminate();
	maliggy_memory_terminate();
#if defined(CONFIG_MALI400_PROFILING)
	_maliggy_osk_profiling_term();
#endif

	/* Allowing the system to be turned off */
	_maliggy_osk_pm_dev_ref_dec();

	maliggy_pp_scheduler_terminate();
	maliggy_session_terminate();
}

_maliggy_product_id_t maliggy_kernel_core_get_product_id(void)
{
	return global_product_id;
}

u32 maliggy_kernel_core_get_gpu_major_version(void)
{
    return global_gpu_major_version;
}

u32 maliggy_kernel_core_get_gpu_minor_version(void)
{
    return global_gpu_minor_version;
}

_maliggy_osk_errcode_t _maliggy_ukk_get_api_version( _maliggy_uk_get_api_version_s *args )
{
	MALI_DEBUG_ASSERT_POINTER(args);
	MALI_CHECK_NON_NULL(args->ctx, _MALI_OSK_ERR_INVALID_ARGS);

	/* check compatability */
	if ( args->version == _MALI_UK_API_VERSION )
	{
		args->compatible = 1;
	}
	else
	{
		args->compatible = 0;
	}

	args->version = _MALI_UK_API_VERSION; /* report our version */

	/* success regardless of being compatible or not */
	MALI_SUCCESS;
}

_maliggy_osk_errcode_t _maliggy_ukk_wait_for_notification( _maliggy_uk_wait_for_notification_s *args )
{
	_maliggy_osk_errcode_t err;
	_maliggy_osk_notification_t * notification;
	_maliggy_osk_notification_queue_t *queue;

	/* check input */
	MALI_DEBUG_ASSERT_POINTER(args);
	MALI_CHECK_NON_NULL(args->ctx, _MALI_OSK_ERR_INVALID_ARGS);

	queue = ((struct maliggy_session_data *)args->ctx)->ioctl_queue;

	/* if the queue does not exist we're currently shutting down */
	if (NULL == queue)
	{
		MALI_DEBUG_PRINT(1, ("No notification queue registered with the session. Asking userspace to stop querying\n"));
		args->type = _MALI_NOTIFICATION_CORE_SHUTDOWN_IN_PROGRESS;
		MALI_SUCCESS;
	}

	/* receive a notification, might sleep */
	err = _maliggy_osk_notification_queue_receive(queue, &notification);
	if (_MALI_OSK_ERR_OK != err)
	{
		MALI_ERROR(err); /* errcode returned, pass on to caller */
	}

	/* copy the buffer to the user */
	args->type = (_maliggy_uk_notification_type)notification->notification_type;
	_maliggy_osk_memcpy(&args->data, notification->result_buffer, notification->result_buffer_size);

	/* finished with the notification */
	_maliggy_osk_notification_delete( notification );

	MALI_SUCCESS; /* all ok */
}

_maliggy_osk_errcode_t _maliggy_ukk_post_notification( _maliggy_uk_post_notification_s *args )
{
	_maliggy_osk_notification_t * notification;
	_maliggy_osk_notification_queue_t *queue;

	/* check input */
	MALI_DEBUG_ASSERT_POINTER(args);
	MALI_CHECK_NON_NULL(args->ctx, _MALI_OSK_ERR_INVALID_ARGS);

	queue = ((struct maliggy_session_data *)args->ctx)->ioctl_queue;

	/* if the queue does not exist we're currently shutting down */
	if (NULL == queue)
	{
		MALI_DEBUG_PRINT(1, ("No notification queue registered with the session. Asking userspace to stop querying\n"));
		MALI_SUCCESS;
	}

	notification = _maliggy_osk_notification_create(args->type, 0);
	if (NULL == notification)
	{
		MALI_PRINT_ERROR( ("Failed to create notification object\n"));
		return _MALI_OSK_ERR_NOMEM;
	}

	_maliggy_osk_notification_queue_send(queue, notification);

	MALI_SUCCESS; /* all ok */
}

_maliggy_osk_errcode_t _maliggy_ukk_open(void **context)
{
	struct maliggy_session_data *session;

	/* allocated struct to track this session */
	session = (struct maliggy_session_data *)_maliggy_osk_calloc(1, sizeof(struct maliggy_session_data));
	MALI_CHECK_NON_NULL(session, _MALI_OSK_ERR_NOMEM);

	MALI_DEBUG_PRINT(3, ("Session starting\n"));

	/* create a response queue for this session */
	session->ioctl_queue = _maliggy_osk_notification_queue_init();
	if (NULL == session->ioctl_queue)
	{
		_maliggy_osk_free(session);
		MALI_ERROR(_MALI_OSK_ERR_NOMEM);
	}

	session->page_directory = maliggy_mmu_pagedir_alloc();
	if (NULL == session->page_directory)
	{
		_maliggy_osk_notification_queue_term(session->ioctl_queue);
		_maliggy_osk_free(session);
		MALI_ERROR(_MALI_OSK_ERR_NOMEM);
	}

	if (_MALI_OSK_ERR_OK != maliggy_mmu_pagedir_map(session->page_directory, MALI_DLBU_VIRT_ADDR, _MALI_OSK_MALI_PAGE_SIZE))
	{
		MALI_PRINT_ERROR(("Failed to map DLBU page into session\n"));
		_maliggy_osk_notification_queue_term(session->ioctl_queue);
		_maliggy_osk_free(session);
		MALI_ERROR(_MALI_OSK_ERR_NOMEM);
	}

	if (0 != maliggy_dlbu_phys_addr)
	{
		maliggy_mmu_pagedir_update(session->page_directory, MALI_DLBU_VIRT_ADDR, maliggy_dlbu_phys_addr,
		                        _MALI_OSK_MALI_PAGE_SIZE, MALI_CACHE_STANDARD);
	}

	if (_MALI_OSK_ERR_OK != maliggy_memory_session_begin(session))
	{
		maliggy_mmu_pagedir_free(session->page_directory);
		_maliggy_osk_notification_queue_term(session->ioctl_queue);
		_maliggy_osk_free(session);
		MALI_ERROR(_MALI_OSK_ERR_NOMEM);
	}

#ifdef CONFIG_SYNC
	_maliggy_osk_list_init(&session->pending_jobs);
	session->pending_jobs_lock = _maliggy_osk_lock_init(_MALI_OSK_LOCKFLAG_NONINTERRUPTABLE | _MALI_OSK_LOCKFLAG_ORDERED | _MALI_OSK_LOCKFLAG_SPINLOCK,
	                                                 0, _MALI_OSK_LOCK_ORDER_SESSION_PENDING_JOBS);
	if (NULL == session->pending_jobs_lock)
	{
		MALI_PRINT_ERROR(("Failed to create pending jobs lock\n"));
		maliggy_memory_session_end(session);
		maliggy_mmu_pagedir_free(session->page_directory);
		_maliggy_osk_notification_queue_term(session->ioctl_queue);
		_maliggy_osk_free(session);
		MALI_ERROR(_MALI_OSK_ERR_NOMEM);
	}
#endif

	*context = (void*)session;

	/* Add session to the list of all sessions. */
	maliggy_session_add(session);

	/* Initialize list of jobs on this session */
	_MALI_OSK_INIT_LIST_HEAD(&session->job_list);

	MALI_DEBUG_PRINT(2, ("Session started\n"));
	MALI_SUCCESS;
}

_maliggy_osk_errcode_t _maliggy_ukk_close(void **context)
{
	struct maliggy_session_data *session;
	MALI_CHECK_NON_NULL(context, _MALI_OSK_ERR_INVALID_ARGS);
	session = (struct maliggy_session_data *)*context;

	MALI_DEBUG_PRINT(3, ("Session ending\n"));

	/* Remove session from list of all sessions. */
	maliggy_session_remove(session);

	/* Abort pending jobs */
#ifdef CONFIG_SYNC
	{
		_maliggy_osk_list_t tmp_job_list;
		struct maliggy_pp_job *job, *tmp;
		_MALI_OSK_INIT_LIST_HEAD(&tmp_job_list);

		_maliggy_osk_lock_wait(session->pending_jobs_lock, _MALI_OSK_LOCKMODE_RW);
		/* Abort asynchronous wait on fence. */
		_MALI_OSK_LIST_FOREACHENTRY(job, tmp, &session->pending_jobs, struct maliggy_pp_job, list)
		{
			MALI_DEBUG_PRINT(2, ("Sync: Aborting wait for session %x job %x\n", session, job));
			if (sync_fence_cancel_async(job->pre_fence, &job->sync_waiter))
			{
				MALI_DEBUG_PRINT(2, ("Sync: Failed to abort job %x\n", job));
			}
			_maliggy_osk_list_add(&job->list, &tmp_job_list);
		}
		_maliggy_osk_lock_signal(session->pending_jobs_lock, _MALI_OSK_LOCKMODE_RW);

		_maliggy_osk_wq_flush();

		_maliggy_osk_lock_term(session->pending_jobs_lock);

		/* Delete jobs */
		_MALI_OSK_LIST_FOREACHENTRY(job, tmp, &tmp_job_list, struct maliggy_pp_job, list)
		{
			maliggy_pp_job_delete(job);
		}
	}
#endif

	/* Abort queued and running jobs */
	maliggy_gp_scheduler_abort_session(session);
	maliggy_pp_scheduler_abort_session(session);

	/* Flush pending work.
	 * Needed to make sure all bottom half processing related to this
	 * session has been completed, before we free internal data structures.
	 */
	_maliggy_osk_wq_flush();

	/* Free remaining memory allocated to this session */
	maliggy_memory_session_end(session);

	/* Free session data structures */
	maliggy_mmu_pagedir_free(session->page_directory);
	_maliggy_osk_notification_queue_term(session->ioctl_queue);
	_maliggy_osk_free(session);

	*context = NULL;

	MALI_DEBUG_PRINT(2, ("Session has ended\n"));

	MALI_SUCCESS;
}

#if MALI_STATE_TRACKING
u32 _maliggy_kernel_core_dumpggy_state(char* buf, u32 size)
{
	int n = 0; /* Number of bytes written to buf */

	n += maliggy_gp_scheduler_dumpggy_state(buf + n, size - n);
	n += maliggy_pp_scheduler_dumpggy_state(buf + n, size - n);

	return n;
}
#endif
