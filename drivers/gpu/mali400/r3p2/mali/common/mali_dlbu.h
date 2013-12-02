/*
 * Copyright (C) 2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_DLBU_H__
#define __MALI_DLBU_H__

#define MALI_DLBU_VIRT_ADDR 0xFFF00000 /* master tile virtual address fixed at this value and mapped into every session */

#include "mali_osk.h"

struct maliggy_pp_job;
struct maliggy_group;

extern u32 maliggy_dlbu_phys_addr;

struct maliggy_dlbu_core;

_maliggy_osk_errcode_t maliggy_dlbu_initialize(void);
void maliggy_dlbu_terminate(void);

struct maliggy_dlbu_core *maliggy_dlbu_create(const _maliggy_osk_resource_t * resource);
void maliggy_dlbu_delete(struct maliggy_dlbu_core *dlbu);

_maliggy_osk_errcode_t maliggy_dlbu_reset(struct maliggy_dlbu_core *dlbu);

void maliggy_dlbu_add_group(struct maliggy_dlbu_core *dlbu, struct maliggy_group *group);
void maliggy_dlbu_remove_group(struct maliggy_dlbu_core *dlbu, struct maliggy_group *group);

/** @brief Called to update HW after DLBU state changed
 *
 * This function must be called after \a maliggy_dlbu_add_group or \a
 * maliggy_dlbu_remove_group to write the updated mask to hardware, unless the
 * same is accomplished by calling \a maliggy_dlbu_reset.
 */
void maliggy_dlbu_update_mask(struct maliggy_dlbu_core *dlbu);

void maliggy_dlbu_config_job(struct maliggy_dlbu_core *dlbu, struct maliggy_pp_job *job);

#endif /* __MALI_DLBU_H__ */
