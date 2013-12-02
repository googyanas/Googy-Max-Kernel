/*
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_KERNEL_CORE_H__
#define __MALI_KERNEL_CORE_H__

#include "mali_osk.h"

extern int maliggy_max_job_runtime;

typedef enum
{
	_MALI_PRODUCT_ID_UNKNOWN,
	_MALI_PRODUCT_ID_MALI200,
	_MALI_PRODUCT_ID_MALI300,
	_MALI_PRODUCT_ID_MALI400,
	_MALI_PRODUCT_ID_MALI450,
} _maliggy_product_id_t;

_maliggy_osk_errcode_t maliggy_initialize_subsystems(void);

void maliggy_terminate_subsystems(void);

_maliggy_product_id_t maliggy_kernel_core_get_product_id(void);

u32 maliggy_kernel_core_get_gpu_major_version(void);

u32 maliggy_kernel_core_get_gpu_minor_version(void);

u32 _maliggy_kernel_core_dumpggy_state(char* buf, u32 size);

MALI_STATIC_INLINE maliggy_bool maliggy_is_maliggy450(void)
{
	return _MALI_PRODUCT_ID_MALI450 == maliggy_kernel_core_get_product_id();
}

MALI_STATIC_INLINE maliggy_bool maliggy_is_maliggy400(void)
{
	u32 id = maliggy_kernel_core_get_product_id();
	return _MALI_PRODUCT_ID_MALI400 == id || _MALI_PRODUCT_ID_MALI300 == id;
}

#endif /* __MALI_KERNEL_CORE_H__ */

