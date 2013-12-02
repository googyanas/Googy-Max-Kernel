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
#include "mali_osk_bitops.h"
#include "mali_osk_list.h"
#include "ump_osk.h"
#include "ump_uk_types.h"
#include "ump_ukk.h"
#include "ump_kernel_common.h"
#include "ump_kernel_descriptor_mapping.h"
#include "ump_kernel_memory_backend.h"



/**
 * Define the initial and maximum size of number of secure_ids on the system
 */
#define UMP_SECURE_ID_TABLE_ENTRIES_INITIAL (128  )
#define UMP_SECURE_ID_TABLE_ENTRIES_MAXIMUM (4096 )


/**
 * Define the initial and maximum size of the umpggy_session_data::cookies_map,
 * which is a \ref umpggy_descriptor_mapping. This limits how many secure_ids
 * may be mapped into a particular process using _umpggy_ukk_map_mem().
 */

#define UMP_COOKIES_PER_SESSION_INITIAL (UMP_SECURE_ID_TABLE_ENTRIES_INITIAL )
#define UMP_COOKIES_PER_SESSION_MAXIMUM (UMP_SECURE_ID_TABLE_ENTRIES_MAXIMUM)

struct umpggy_dev device;

_maliggy_osk_errcode_t umpggy_kernel_constructor(void)
{
	_maliggy_osk_errcode_t err;

	/* Perform OS Specific initialization */
	err = _umpggy_osk_init();
	if( _MALI_OSK_ERR_OK != err )
	{
		MSG_ERR(("Failed to initiaze the UMP Device Driver"));
		return err;
	}

	/* Init the global device */
	_maliggy_osk_memset(&device, 0, sizeof(device) );

	/* Create the descriptor map, which will be used for mapping secure ID to umpggy_dd_mem structs */
	device.secure_id_map_lock = _maliggy_osk_lock_init(_MALI_OSK_LOCKFLAG_NONINTERRUPTABLE, 0 , 0);
	if (NULL == device.secure_id_map_lock)
	{
		MSG_ERR(("Failed to create OSK lock for secure id lookup table\n"));
		return _MALI_OSK_ERR_NOMEM;
	}

	device.secure_id_map = umpggy_descriptor_mapping_create(UMP_SECURE_ID_TABLE_ENTRIES_INITIAL, UMP_SECURE_ID_TABLE_ENTRIES_MAXIMUM);
	if (NULL == device.secure_id_map)
	{
		_maliggy_osk_lock_term(device.secure_id_map_lock);
		MSG_ERR(("Failed to create secure id lookup table\n"));
		return _MALI_OSK_ERR_NOMEM;
	}

	/* Init memory backend */
	device.backend = umpggy_memory_backend_create();
	if (NULL == device.backend)
	{
		MSG_ERR(("Failed to create memory backend\n"));
		_maliggy_osk_lock_term(device.secure_id_map_lock);
		umpggy_descriptor_mapping_destroy(device.secure_id_map);
		return _MALI_OSK_ERR_NOMEM;
	}

	return _MALI_OSK_ERR_OK;
}

void umpggy_kernel_destructor(void)
{
	DEBUG_ASSERT_POINTER(device.secure_id_map);
	DEBUG_ASSERT_POINTER(device.secure_id_map_lock);

	_maliggy_osk_lock_term(device.secure_id_map_lock);
	device.secure_id_map_lock = NULL;

	umpggy_descriptor_mapping_destroy(device.secure_id_map);
	device.secure_id_map = NULL;

	device.backend->shutdown(device.backend);
	device.backend = NULL;

	umpggy_memory_backend_destroy();

	_umpggy_osk_term();
}

/** Creates a new UMP session
 */
_maliggy_osk_errcode_t _umpggy_ukk_open( void** context )
{
	struct umpggy_session_data * session_data;

	/* allocated struct to track this session */
	session_data = (struct umpggy_session_data *)_maliggy_osk_malloc(sizeof(struct umpggy_session_data));
	if (NULL == session_data)
	{
		MSG_ERR(("Failed to allocate umpggy_session_data in umpggy_file_open()\n"));
		return _MALI_OSK_ERR_NOMEM;
	}

	session_data->lock = _maliggy_osk_lock_init(_MALI_OSK_LOCKFLAG_NONINTERRUPTABLE, 0, 0);
	if( NULL == session_data->lock )
	{
		MSG_ERR(("Failed to initialize lock for umpggy_session_data in umpggy_file_open()\n"));
		_maliggy_osk_free(session_data);
		return _MALI_OSK_ERR_NOMEM;
	}

	session_data->cookies_map = umpggy_descriptor_mapping_create( UMP_COOKIES_PER_SESSION_INITIAL, UMP_COOKIES_PER_SESSION_MAXIMUM );

	if ( NULL == session_data->cookies_map )
	{
		MSG_ERR(("Failed to create descriptor mapping for _umpggy_ukk_map_mem cookies\n"));

		_maliggy_osk_lock_term( session_data->lock );
		_maliggy_osk_free( session_data );
		return _MALI_OSK_ERR_NOMEM;
	}

	_MALI_OSK_INIT_LIST_HEAD(&session_data->list_head_session_memory_list);

	_MALI_OSK_INIT_LIST_HEAD(&session_data->list_head_session_memory_mappings_list);

	/* Since initial version of the UMP interface did not use the API_VERSION ioctl we have to assume
	   that it is this version, and not the "latest" one: UMP_IOCTL_API_VERSION
	   Current and later API versions would do an additional call to this IOCTL and update this variable
	   to the correct one.*/
	session_data->api_version = MAKE_VERSION_ID(1);

	*context = (void*)session_data;

	session_data->cache_operations_ongoing = 0 ;
	session_data->has_pending_level1_cache_flush = 0;

	DBG_MSG(2, ("New session opened\n"));

	return _MALI_OSK_ERR_OK;
}

_maliggy_osk_errcode_t _umpggy_ukk_close( void** context )
{
	struct umpggy_session_data * session_data;
	umpggy_session_memory_list_element * item;
	umpggy_session_memory_list_element * tmp;

	session_data = (struct umpggy_session_data *)*context;
	if (NULL == session_data)
	{
		MSG_ERR(("Session data is NULL in _umpggy_ukk_close()\n"));
		return _MALI_OSK_ERR_INVALID_ARGS;
	}

	/* Unmap any descriptors mapped in. */
	if (0 == _maliggy_osk_list_empty(&session_data->list_head_session_memory_mappings_list))
	{
		umpggy_memory_allocation *descriptor;
		umpggy_memory_allocation *temp;

		DBG_MSG(1, ("Memory mappings found on session usage list during session termination\n"));

		/* use the 'safe' list iterator, since freeing removes the active block from the list we're iterating */
		_MALI_OSK_LIST_FOREACHENTRY(descriptor, temp, &session_data->list_head_session_memory_mappings_list, umpggy_memory_allocation, list)
		{
			_umpggy_uk_unmap_mem_s unmap_args;
			DBG_MSG(4, ("Freeing block with phys address 0x%x size 0x%x mapped in user space at 0x%x\n",
			            descriptor->phys_addr, descriptor->size, descriptor->mapping));
			unmap_args.ctx = (void*)session_data;
			unmap_args.mapping = descriptor->mapping;
			unmap_args.size = descriptor->size;
			unmap_args._ukk_private = NULL; /* NOTE: unused */
			unmap_args.cookie = descriptor->cookie;

			/* NOTE: This modifies the list_head_session_memory_mappings_list */
			_umpggy_ukk_unmap_mem( &unmap_args );
		}
	}

	/* ASSERT that we really did free everything, because _umpggy_ukk_unmap_mem()
	 * can fail silently. */
	DEBUG_ASSERT( _maliggy_osk_list_empty(&session_data->list_head_session_memory_mappings_list) );

	_MALI_OSK_LIST_FOREACHENTRY(item, tmp, &session_data->list_head_session_memory_list, umpggy_session_memory_list_element, list)
	{
		_maliggy_osk_list_del(&item->list);
		DBG_MSG(2, ("Releasing UMP memory %u as part of file close\n", item->mem->secure_id));
		umpggy_dd_reference_release(item->mem);
		_maliggy_osk_free(item);
	}

	umpggy_descriptor_mapping_destroy( session_data->cookies_map );

	_maliggy_osk_lock_term(session_data->lock);
	_maliggy_osk_free(session_data);

	DBG_MSG(2, ("Session closed\n"));

	return _MALI_OSK_ERR_OK;
}

_maliggy_osk_errcode_t _umpggy_ukk_map_mem( _umpggy_uk_map_mem_s *args )
{
	struct umpggy_session_data * session_data;
	umpggy_memory_allocation * descriptor;  /* Describes current mapping of memory */
	_maliggy_osk_errcode_t err;
	unsigned long offset = 0;
	unsigned long left;
	umpggy_dd_handle handle;  /* The real UMP handle for this memory. Its real datatype is umpggy_dd_mem*  */
	umpggy_dd_mem * mem;      /* The real UMP memory. It is equal to the handle, but with exposed struct */
	u32 block;
	int map_id;

	session_data = (umpggy_session_data *)args->ctx;
	if( NULL == session_data )
	{
		MSG_ERR(("Session data is NULL in _umpggy_ukk_map_mem()\n"));
		return _MALI_OSK_ERR_INVALID_ARGS;
	}
	/* MALI_SEC */
	/* SEC kernel stability 2012-02-17 */
	if (NULL == session_data->cookies_map)
	{
		MSG_ERR(("session_data->cookies_map is NULL in _umpggy_ukk_map_mem()\n"));
		return _MALI_OSK_ERR_INVALID_ARGS;
	}
	descriptor = (umpggy_memory_allocation*) _maliggy_osk_calloc( 1, sizeof(umpggy_memory_allocation));
	if (NULL == descriptor)
	{
		MSG_ERR(("ump_ukk_map_mem: descriptor allocation failed\n"));
		return _MALI_OSK_ERR_NOMEM;
	}

	handle = umpggy_dd_handle_create_from_secure_id(args->secure_id);
	if ( UMP_DD_HANDLE_INVALID == handle)
	{
		_maliggy_osk_free(descriptor);
		DBG_MSG(1, ("Trying to map unknown secure ID %u\n", args->secure_id));
		return _MALI_OSK_ERR_FAULT;
	}

	mem = (umpggy_dd_mem*)handle;
	DEBUG_ASSERT(mem);
	if (mem->size_bytes != args->size)
	{
		_maliggy_osk_free(descriptor);
		umpggy_dd_reference_release(handle);
		DBG_MSG(1, ("Trying to map too much or little. ID: %u, virtual size=%lu, UMP size: %lu\n", args->secure_id, args->size, mem->size_bytes));
		return _MALI_OSK_ERR_FAULT;
	}

	map_id = umpggy_descriptor_mapping_allocate_mapping( session_data->cookies_map, (void*) descriptor );

	if (map_id < 0)
	{
		_maliggy_osk_free(descriptor);
		umpggy_dd_reference_release(handle);
		DBG_MSG(1, ("ump_ukk_map_mem: unable to allocate a descriptor_mapping for return cookie\n"));

		return _MALI_OSK_ERR_NOMEM;
	}

	descriptor->size = args->size;
	descriptor->handle = handle;
	descriptor->phys_addr = args->phys_addr;
	descriptor->process_mapping_info = args->_ukk_private;
	descriptor->umpggy_session = session_data;
	descriptor->cookie = (u32)map_id;

	if ( mem->is_cached )
	{
		descriptor->is_cached = 1;
		args->is_cached       = 1;
		DBG_MSG(3, ("Mapping UMP secure_id: %d as cached.\n", args->secure_id));
	}
	/* MALI_SEC */
	else if ( args->is_cached)
	{
		mem->is_cached = 1;
		descriptor->is_cached = 1;
		DBG_MSG(3, ("Warning mapping UMP secure_id: %d. As cached, while it was allocated uncached.\n", args->secure_id));
	}
	else
	{
		descriptor->is_cached = 0;
		args->is_cached       = 0;
		DBG_MSG(3, ("Mapping UMP secure_id: %d  as Uncached.\n", args->secure_id));
	}

	_maliggy_osk_list_init( &descriptor->list );

	err = _umpggy_osk_mem_mapregion_init( descriptor );
	if( _MALI_OSK_ERR_OK != err )
	{
		DBG_MSG(1, ("Failed to initialize memory mapping in _umpggy_ukk_map_mem(). ID: %u\n", args->secure_id));
		umpggy_descriptor_mapping_free( session_data->cookies_map, map_id );
		_maliggy_osk_free(descriptor);
		umpggy_dd_reference_release(mem);
		return err;
	}

	DBG_MSG(4, ("Mapping virtual to physical memory: ID: %u, size:%lu, first physical addr: 0x%08lx, number of regions: %lu\n",
	        mem->secure_id,
	        mem->size_bytes,
	        ((NULL != mem->block_array) ? mem->block_array->addr : 0),
	        mem->nr_blocks));

	left = descriptor->size;
	/* loop over all blocks and map them in */
	for (block = 0; block < mem->nr_blocks; block++)
	{
		unsigned long size_to_map;

		if (left >  mem->block_array[block].size)
		{
			size_to_map = mem->block_array[block].size;
		}
		else
		{
			size_to_map = left;
		}

		if (_MALI_OSK_ERR_OK != _umpggy_osk_mem_mapregion_map(descriptor, offset, (u32 *)&(mem->block_array[block].addr), size_to_map ) )
		{
			DBG_MSG(1, ("WARNING: _umpggy_ukk_map_mem failed to map memory into userspace\n"));
			umpggy_descriptor_mapping_free( session_data->cookies_map, map_id );
			umpggy_dd_reference_release(mem);
			_umpggy_osk_mem_mapregion_term( descriptor );
			_maliggy_osk_free(descriptor);
			return _MALI_OSK_ERR_FAULT;
		}
		left -= size_to_map;
		offset += size_to_map;
	}

	/* Add to the umpggy_memory_allocation tracking list */
	_maliggy_osk_lock_wait(session_data->lock, _MALI_OSK_LOCKMODE_RW);
	_maliggy_osk_list_add( &descriptor->list, &session_data->list_head_session_memory_mappings_list );
	_maliggy_osk_lock_signal(session_data->lock, _MALI_OSK_LOCKMODE_RW);

	args->mapping = descriptor->mapping;
	args->cookie = descriptor->cookie;

	return _MALI_OSK_ERR_OK;
}

void _umpggy_ukk_unmap_mem( _umpggy_uk_unmap_mem_s *args )
{
	struct umpggy_session_data * session_data;
	umpggy_memory_allocation * descriptor;
	umpggy_dd_handle handle;

	session_data = (umpggy_session_data *)args->ctx;

	if( NULL == session_data )
	{
		MSG_ERR(("Session data is NULL in _umpggy_ukk_map_mem()\n"));
		return;
	}
	/* MALI_SEC */
	/* SEC kernel stability 2012-02-17 */
	if (NULL == session_data->cookies_map)
	{
		MSG_ERR(("session_data->cookies_map is NULL in _umpggy_ukk_map_mem()\n"));
		return;
	}
	if (0 != umpggy_descriptor_mapping_get( session_data->cookies_map, (int)args->cookie, (void**)&descriptor) )
	{
		MSG_ERR(("_umpggy_ukk_map_mem: cookie 0x%X not found for this session\n", args->cookie ));
		return;
	}

	DEBUG_ASSERT_POINTER(descriptor);

	handle = descriptor->handle;
	if ( UMP_DD_HANDLE_INVALID == handle)
	{
		DBG_MSG(1, ("WARNING: Trying to unmap unknown handle: UNKNOWN\n"));
		return;
	}

	/* Remove the umpggy_memory_allocation from the list of tracked mappings */
	_maliggy_osk_lock_wait(session_data->lock, _MALI_OSK_LOCKMODE_RW);
	_maliggy_osk_list_del( &descriptor->list );
	_maliggy_osk_lock_signal(session_data->lock, _MALI_OSK_LOCKMODE_RW);

	umpggy_descriptor_mapping_free( session_data->cookies_map, (int)args->cookie );

	umpggy_dd_reference_release(handle);

	_umpggy_osk_mem_mapregion_term( descriptor );
	_maliggy_osk_free(descriptor);
}

u32 _umpggy_ukk_report_memory_usage( void )
{
	if(device.backend->stat)
		return device.backend->stat(device.backend);
	else
		return 0;
}
