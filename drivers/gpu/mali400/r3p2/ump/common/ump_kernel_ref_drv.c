/*
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "mali_osk.h"
#include "mali_osk_list.h"
#include "ump_osk.h"
#include "ump_uk_types.h"

#include "ump_kernel_interface_ref_drv.h"
#include "ump_kernel_common.h"
#include "ump_kernel_descriptor_mapping.h"

#define UMP_MINIMUM_SIZE         4096
#define UMP_MINIMUM_SIZE_MASK    (~(UMP_MINIMUM_SIZE-1))
#define UMP_SIZE_ALIGN(x)        (((x)+UMP_MINIMUM_SIZE-1)&UMP_MINIMUM_SIZE_MASK)
#define UMP_ADDR_ALIGN_OFFSET(x) ((x)&(UMP_MINIMUM_SIZE-1))
static void phys_blocks_release(void * ctx, struct umpggy_dd_mem * descriptor);

UMP_KERNEL_API_EXPORT umpggy_dd_handle umpggy_dd_handle_create_from_phys_blocks(umpggy_dd_physical_block * blocks, unsigned long num_blocks)
{
	umpggy_dd_mem * mem;
	unsigned long size_total = 0;
	int map_id;
	u32 i;

	/* Go through the input blocks and verify that they are sane */
	for (i=0; i < num_blocks; i++)
	{
		unsigned long addr = blocks[i].addr;
		unsigned long size = blocks[i].size;

		DBG_MSG(5, ("Adding physical memory to new handle. Address: 0x%08lx, size: %lu\n", addr, size));
		size_total += blocks[i].size;

		if (0 != UMP_ADDR_ALIGN_OFFSET(addr))
		{
			MSG_ERR(("Trying to create UMP memory from unaligned physical address. Address: 0x%08lx\n", addr));
			return UMP_DD_HANDLE_INVALID;
		}

		if (0 != UMP_ADDR_ALIGN_OFFSET(size))
		{
			MSG_ERR(("Trying to create UMP memory with unaligned size. Size: %lu\n", size));
			return UMP_DD_HANDLE_INVALID;
		}
	}

	/* Allocate the umpggy_dd_mem struct for this allocation */
	mem = _maliggy_osk_malloc(sizeof(*mem));
	if (NULL == mem)
	{
		DBG_MSG(1, ("Could not allocate umpggy_dd_mem in umpggy_dd_handle_create_from_phys_blocks()\n"));
		return UMP_DD_HANDLE_INVALID;
	}

	/* Find a secure ID for this allocation */
	_maliggy_osk_lock_wait(device.secure_id_map_lock, _MALI_OSK_LOCKMODE_RW);
	map_id = umpggy_descriptor_mapping_allocate_mapping(device.secure_id_map, (void*) mem);

	if (map_id < 0)
	{
		_maliggy_osk_lock_signal(device.secure_id_map_lock, _MALI_OSK_LOCKMODE_RW);
		_maliggy_osk_free(mem);
		DBG_MSG(1, ("Failed to allocate secure ID in umpggy_dd_handle_create_from_phys_blocks()\n"));
		return UMP_DD_HANDLE_INVALID;
	}

	/* Now, make a copy of the block information supplied by the user */
	mem->block_array = _maliggy_osk_malloc(sizeof(umpggy_dd_physical_block)* num_blocks);
	if (NULL == mem->block_array)
	{
		umpggy_descriptor_mapping_free(device.secure_id_map, map_id);
		_maliggy_osk_lock_signal(device.secure_id_map_lock, _MALI_OSK_LOCKMODE_RW);
		_maliggy_osk_free(mem);
		DBG_MSG(1, ("Could not allocate a mem handle for function umpggy_dd_handle_create_from_phys_blocks().\n"));
		return UMP_DD_HANDLE_INVALID;
	}

	_maliggy_osk_memcpy(mem->block_array, blocks, sizeof(umpggy_dd_physical_block) * num_blocks);

	/* And setup the rest of the umpggy_dd_mem struct */
	_maliggy_osk_atomic_init(&mem->ref_count, 1);
	mem->secure_id = (umpggy_secure_id)map_id;
	mem->size_bytes = size_total;
	mem->nr_blocks = num_blocks;
	mem->backend_info = NULL;
	mem->ctx = NULL;
	mem->release_func = phys_blocks_release;
	/* For now UMP handles created by umpggy_dd_handle_create_from_phys_blocks() is forced to be Uncached */
	mem->is_cached = 0;
	mem->hw_device = _UMP_UK_USED_BY_CPU;
	mem->lock_usage = UMP_NOT_LOCKED;

	_maliggy_osk_lock_signal(device.secure_id_map_lock, _MALI_OSK_LOCKMODE_RW);
	DBG_MSG(3, ("UMP memory created. ID: %u, size: %lu\n", mem->secure_id, mem->size_bytes));

	return (umpggy_dd_handle)mem;
}

static void phys_blocks_release(void * ctx, struct umpggy_dd_mem * descriptor)
{
	_maliggy_osk_free(descriptor->block_array);
	descriptor->block_array = NULL;
}

_maliggy_osk_errcode_t _umpggy_ukk_allocate( _umpggy_uk_allocate_s *user_interaction )
{
	umpggy_session_data * session_data = NULL;
	umpggy_dd_mem *new_allocation = NULL;
	umpggy_session_memory_list_element * session_memory_element = NULL;
	int map_id;

	DEBUG_ASSERT_POINTER( user_interaction );
	DEBUG_ASSERT_POINTER( user_interaction->ctx );

	session_data = (umpggy_session_data *) user_interaction->ctx;

	session_memory_element = _maliggy_osk_calloc( 1, sizeof(umpggy_session_memory_list_element));
	if (NULL == session_memory_element)
	{
		DBG_MSG(1, ("Failed to allocate umpggy_session_memory_list_element in umpggy_ioctl_allocate()\n"));
		return _MALI_OSK_ERR_NOMEM;
	}


	new_allocation = _maliggy_osk_calloc( 1, sizeof(umpggy_dd_mem));
	if (NULL==new_allocation)
	{
		_maliggy_osk_free(session_memory_element);
		DBG_MSG(1, ("Failed to allocate umpggy_dd_mem in _umpggy_ukk_allocate()\n"));
		return _MALI_OSK_ERR_NOMEM;
	}

	/* Create a secure ID for this allocation */
	_maliggy_osk_lock_wait(device.secure_id_map_lock, _MALI_OSK_LOCKMODE_RW);
	map_id = umpggy_descriptor_mapping_allocate_mapping(device.secure_id_map, (void*)new_allocation);

	if (map_id < 0)
	{
		_maliggy_osk_lock_signal(device.secure_id_map_lock, _MALI_OSK_LOCKMODE_RW);
		_maliggy_osk_free(session_memory_element);
		_maliggy_osk_free(new_allocation);
		DBG_MSG(1, ("Failed to allocate secure ID in umpggy_ioctl_allocate()\n"));
		return - _MALI_OSK_ERR_INVALID_FUNC;
	}

	/* Initialize the part of the new_allocation that we know so for */
	new_allocation->secure_id = (umpggy_secure_id)map_id;
	_maliggy_osk_atomic_init(&new_allocation->ref_count,1);
	if ( 0==(UMP_REF_DRV_UK_CONSTRAINT_USE_CACHE & user_interaction->constraints) )
		 new_allocation->is_cached = 0;
	else new_allocation->is_cached = 1;

	/* special case a size of 0, we should try to emulate what malloc does in this case, which is to return a valid pointer that must be freed, but can't be dereferences */
	if (0 == user_interaction->size)
	{
		user_interaction->size = 1; /* emulate by actually allocating the minimum block size */
	}

	new_allocation->size_bytes = UMP_SIZE_ALIGN(user_interaction->size); /* Page align the size */
	new_allocation->lock_usage = UMP_NOT_LOCKED;

	/* Now, ask the active memory backend to do the actual memory allocation */
	if (!device.backend->allocate( device.backend->ctx, new_allocation ) )
	{
		DBG_MSG(3, ("OOM: No more UMP memory left. Failed to allocate memory in umpggy_ioctl_allocate(). Size: %lu, requested size: %lu\n", new_allocation->size_bytes, (unsigned long)user_interaction->size));
		umpggy_descriptor_mapping_free(device.secure_id_map, map_id);
		_maliggy_osk_lock_signal(device.secure_id_map_lock, _MALI_OSK_LOCKMODE_RW);
		_maliggy_osk_free(new_allocation);
		_maliggy_osk_free(session_memory_element);
		return _MALI_OSK_ERR_INVALID_FUNC;
	}
	new_allocation->hw_device = _UMP_UK_USED_BY_CPU;
	new_allocation->ctx = device.backend->ctx;
	new_allocation->release_func = device.backend->release;

	_maliggy_osk_lock_signal(device.secure_id_map_lock, _MALI_OSK_LOCKMODE_RW);

	/* Initialize the session_memory_element, and add it to the session object */
	session_memory_element->mem = new_allocation;
	_maliggy_osk_lock_wait(session_data->lock, _MALI_OSK_LOCKMODE_RW);
	_maliggy_osk_list_add(&(session_memory_element->list), &(session_data->list_head_session_memory_list));
	_maliggy_osk_lock_signal(session_data->lock, _MALI_OSK_LOCKMODE_RW);

	user_interaction->secure_id = new_allocation->secure_id;
	user_interaction->size = new_allocation->size_bytes;
	DBG_MSG(3, ("UMP memory allocated. ID: %u, size: %lu\n", new_allocation->secure_id, new_allocation->size_bytes));

	return _MALI_OSK_ERR_OK;
}

/* MALI_SEC */
UMP_KERNEL_API_EXPORT umpggy_dd_status_code umpggy_dd_meminfo_set(umpggy_dd_handle memh, void* args)
{
	umpggy_dd_mem * mem;
	umpggy_secure_id secure_id;

	DEBUG_ASSERT_POINTER(memh);

	secure_id = umpggy_dd_secure_id_get(memh);

	_maliggy_osk_lock_wait(device.secure_id_map_lock, _MALI_OSK_LOCKMODE_RW);
	if (0 == umpggy_descriptor_mapping_get(device.secure_id_map, (int)secure_id, (void**)&mem))
	{
		device.backend->set(mem, args);
	}
	else
	{
		_maliggy_osk_lock_signal(device.secure_id_map_lock, _MALI_OSK_LOCKMODE_RW);
		DBG_MSG(1, ("Failed to look up mapping in umpggy_meminfo_set(). ID: %u\n", (umpggy_secure_id)secure_id));
		return UMP_DD_INVALID;
	}

	_maliggy_osk_lock_signal(device.secure_id_map_lock, _MALI_OSK_LOCKMODE_RW);

	return UMP_DD_SUCCESS;
}

UMP_KERNEL_API_EXPORT void *umpggy_dd_meminfo_get(umpggy_secure_id secure_id, void* args)
{
	umpggy_dd_mem * mem;
	void *result;

	_maliggy_osk_lock_wait(device.secure_id_map_lock, _MALI_OSK_LOCKMODE_RW);
	if (0 == umpggy_descriptor_mapping_get(device.secure_id_map, (int)secure_id, (void**)&mem))
	{
		result = device.backend->get(mem, args);
	}
	else
	{
		_maliggy_osk_lock_signal(device.secure_id_map_lock, _MALI_OSK_LOCKMODE_RW);
		DBG_MSG(1, ("Failed to look up mapping in umpggy_meminfo_get(). ID: %u\n", (umpggy_secure_id)secure_id));
		return UMP_DD_HANDLE_INVALID;
	}

	_maliggy_osk_lock_signal(device.secure_id_map_lock, _MALI_OSK_LOCKMODE_RW);

	return result;
}

UMP_KERNEL_API_EXPORT umpggy_dd_handle umpggy_dd_handle_get_from_vaddr(unsigned long vaddr)
{
	umpggy_dd_mem * mem;

	DBG_MSG(5, ("Getting handle from Virtual address. vaddr: %u\n", vaddr));

	_umpggy_osk_mem_mapregion_get(&mem, vaddr);

	DBG_MSG(1, ("Getting handle's Handle : 0x%8lx\n", mem));

	return (umpggy_dd_handle)mem;
}
