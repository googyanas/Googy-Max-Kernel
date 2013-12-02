/*
 * Copyright (C) 2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "mali_broadcast.h"
#include "mali_kernel_common.h"
#include "mali_osk.h"

static const int bcast_unit_reg_size = 0x1000;
static const int bcast_unit_addr_broadcast_mask = 0x0;
static const int bcast_unit_addr_irq_override_mask = 0x4;

struct maliggy_bcast_unit
{
	struct maliggy_hw_core hw_core;
	u32 current_mask;
};

struct maliggy_bcast_unit *maliggy_bcast_unit_create(const _maliggy_osk_resource_t *resource)
{
	struct maliggy_bcast_unit *bcast_unit = NULL;

	MALI_DEBUG_ASSERT_POINTER(resource);
	MALI_DEBUG_PRINT(2, ("Mali Broadcast unit: Creating Mali Broadcast unit: %s\n", resource->description));

	bcast_unit = _maliggy_osk_malloc(sizeof(struct maliggy_bcast_unit));
	if (NULL == bcast_unit)
	{
		MALI_PRINT_ERROR(("Mali Broadcast unit: Failed to allocate memory for Broadcast unit\n"));
		return NULL;
	}

	if (_MALI_OSK_ERR_OK == maliggy_hw_core_create(&bcast_unit->hw_core, resource, bcast_unit_reg_size))
	{
		bcast_unit->current_mask = 0;
		maliggy_bcast_reset(bcast_unit);

		return bcast_unit;
	}
	else
	{
		MALI_PRINT_ERROR(("Mali Broadcast unit: Failed map broadcast unit\n"));
	}

	_maliggy_osk_free(bcast_unit);

	return NULL;
}

void maliggy_bcast_unit_delete(struct maliggy_bcast_unit *bcast_unit)
{
	MALI_DEBUG_ASSERT_POINTER(bcast_unit);

	maliggy_hw_core_delete(&bcast_unit->hw_core);
	_maliggy_osk_free(bcast_unit);
}

void maliggy_bcast_add_group(struct maliggy_bcast_unit *bcast_unit, struct maliggy_group *group)
{
	u32 bcast_id;
	u32 broadcast_mask;

	MALI_DEBUG_ASSERT_POINTER(bcast_unit);
	MALI_DEBUG_ASSERT_POINTER(group);

	bcast_id = maliggy_pp_core_get_bcast_id(maliggy_group_get_pp_core(group));

	broadcast_mask = bcast_unit->current_mask;

	broadcast_mask |= (bcast_id); /* add PP core to broadcast */
	broadcast_mask |= (bcast_id << 16); /* add MMU to broadcast */

	/* store mask so we can restore on reset */
	bcast_unit->current_mask = broadcast_mask;
}

void maliggy_bcast_remove_group(struct maliggy_bcast_unit *bcast_unit, struct maliggy_group *group)
{
	u32 bcast_id;
	u32 broadcast_mask;

	MALI_DEBUG_ASSERT_POINTER(bcast_unit);
	MALI_DEBUG_ASSERT_POINTER(group);

	bcast_id = maliggy_pp_core_get_bcast_id(maliggy_group_get_pp_core(group));

	broadcast_mask = bcast_unit->current_mask;

	broadcast_mask &= ~((bcast_id << 16) | bcast_id);

	/* store mask so we can restore on reset */
	bcast_unit->current_mask = broadcast_mask;
}

void maliggy_bcast_reset(struct maliggy_bcast_unit *bcast_unit)
{
	MALI_DEBUG_ASSERT_POINTER(bcast_unit);

	/* set broadcast mask */
	maliggy_hw_core_register_write(&bcast_unit->hw_core,
	                            bcast_unit_addr_broadcast_mask,
	                            bcast_unit->current_mask);

	/* set IRQ override mask */
	maliggy_hw_core_register_write(&bcast_unit->hw_core,
	                            bcast_unit_addr_irq_override_mask,
	                            bcast_unit->current_mask & 0xFF);
}

void maliggy_bcast_disable(struct maliggy_bcast_unit *bcast_unit)
{
	MALI_DEBUG_ASSERT_POINTER(bcast_unit);

	/* set broadcast mask */
	maliggy_hw_core_register_write(&bcast_unit->hw_core,
	                            bcast_unit_addr_broadcast_mask,
	                            0x0);

	/* set IRQ override mask */
	maliggy_hw_core_register_write(&bcast_unit->hw_core,
	                            bcast_unit_addr_irq_override_mask,
	                            0x0);
}
