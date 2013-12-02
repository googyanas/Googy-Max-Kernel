/*
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_KERNEL_MEMORY_ENGINE_H__
#define  __MALI_KERNEL_MEMORY_ENGINE_H__

typedef void * maliggy_allocation_engine;

typedef enum { MALI_MEM_ALLOC_FINISHED, MALI_MEM_ALLOC_PARTIAL, MALI_MEM_ALLOC_NONE, MALI_MEM_ALLOC_INTERNAL_FAILURE } maliggy_physical_memory_allocation_result;

typedef struct maliggy_physical_memory_allocation
{
	void (*release)(void * ctx, void * handle); /**< Function to call on to release the physical memory */
	void * ctx;
	void * handle;
	struct maliggy_physical_memory_allocation * next;
} maliggy_physical_memory_allocation;

struct maliggy_page_table_block;

typedef struct maliggy_page_table_block
{
	void (*release)(struct maliggy_page_table_block *page_table_block);
	void * ctx;
	void * handle;
	u32 size; /**< In bytes, should be a multiple of MALI_MMU_PAGE_SIZE to avoid internal fragementation */
	u32 phys_base; /**< Mali physical address */
	maliggy_io_address mapping;
} maliggy_page_table_block;


/** @addtogroup _maliggy_osk_low_level_memory
 * @{ */

typedef enum
{
	MALI_MEMORY_ALLOCATION_FLAG_MAP_INTO_USERSPACE = 0x1,
	MALI_MEMORY_ALLOCATION_FLAG_MAP_GUARD_PAGE     = 0x2,
} maliggy_memory_allocation_flag;

/**
 * Supplying this 'magic' physical address requests that the OS allocate the
 * physical address at page commit time, rather than committing a specific page
 */
#define MALI_MEMORY_ALLOCATION_OS_ALLOCATED_PHYSADDR_MAGIC ((u32)(-1))

typedef struct maliggy_memory_allocation
{
	/* Information about the allocation */
	void * mapping; /**< CPU virtual address where the memory is mapped at */
	u32 maliggy_address; /**< The Mali seen address of the memory allocation */
	u32 size; /**< Size of the allocation */
	u32 permission; /**< Permission settings */
	maliggy_memory_allocation_flag flags;
	u32 cache_settings; /* type: maliggy_memory_cache_settings, found in <linux/mali/mali_utgard_uk_types.h> Ump DD breaks if we include it...*/

	_maliggy_osk_lock_t * lock;

	/* Manager specific information pointers */
	void * maliggy_addr_mapping_info; /**< Mali address allocation specific info */
	void * process_addr_mapping_info; /**< Mapping manager specific info */

	maliggy_physical_memory_allocation physical_allocation;

	_maliggy_osk_list_t list; /**< List for linking together memory allocations into the session's memory head */
} maliggy_memory_allocation;
/** @} */ /* end group _maliggy_osk_low_level_memory */


typedef struct maliggy_physical_memory_allocator
{
	maliggy_physical_memory_allocation_result (*allocate)(void* ctx, maliggy_allocation_engine * engine, maliggy_memory_allocation * descriptor, u32* offset, maliggy_physical_memory_allocation * alloc_info);
	maliggy_physical_memory_allocation_result (*allocate_page_table_block)(void * ctx, maliggy_page_table_block * block); /* MALI_MEM_ALLOC_PARTIAL not allowed */
	void (*destroy)(struct maliggy_physical_memory_allocator * allocator);
	u32 (*stat)(struct maliggy_physical_memory_allocator * allocator);
	void * ctx;
	const char * name; /**< Descriptive name for use in maliggy_allocation_engine_report_allocators, or NULL */
	u32 alloc_order; /**< Order in which the allocations should happen */
	struct maliggy_physical_memory_allocator * next;
} maliggy_physical_memory_allocator;

typedef struct maliggy_kernel_mem_address_manager
{
	_maliggy_osk_errcode_t (*allocate)(maliggy_memory_allocation *); /**< Function to call to reserve an address */
	void (*release)(maliggy_memory_allocation *); /**< Function to call to free the address allocated */

	 /**
	  * Function called for each physical sub allocation.
	  * Called for each physical block allocated by the physical memory manager.
	  * @param[in] descriptor The memory descriptor in question
	  * @param[in] off Offset from the start of range
	  * @param[in,out] phys_addr A pointer to the physical address of the start of the
	  * physical block. When *phys_addr == MALI_MEMORY_ALLOCATION_OS_ALLOCATED_PHYSADDR_MAGIC
	  * is used, this requests the function to allocate the physical page
	  * itself, and return it through the pointer provided.
	  * @param[in] size Length in bytes of the physical block
	  * @return _MALI_OSK_ERR_OK on success.
	  * A value of type _maliggy_osk_errcode_t other than _MALI_OSK_ERR_OK indicates failure.
	  * Specifically, _MALI_OSK_ERR_UNSUPPORTED indicates that the function
	  * does not support allocating physical pages itself.
	  */
	 _maliggy_osk_errcode_t (*map_physical)(maliggy_memory_allocation * descriptor, u32 offset, u32 *phys_addr, u32 size);

	 /**
	  * Function called to remove a physical sub allocation.
	  * Called on error paths where one of the address managers fails.
	  *
	  * @note this is optional. For address managers where this is not
	  * implemented, the value of this member is NULL. The memory engine
	  * currently does not require the mali address manager to be able to
	  * unmap individual pages, but the process address manager must have this
	  * capability.
	  *
	  * @param[in] descriptor The memory descriptor in question
	  * @param[in] off Offset from the start of range
	  * @param[in] size Length in bytes of the physical block
	  * @param[in] flags flags to use on a per-page basis. For OS-allocated
	  * physical pages, this must include _MALI_OSK_MEM_MAPREGION_FLAG_OS_ALLOCATED_PHYSADDR.
	  * @return _MALI_OSK_ERR_OK on success.
	  * A value of type _maliggy_osk_errcode_t other than _MALI_OSK_ERR_OK indicates failure.
	  */
	void (*unmap_physical)(maliggy_memory_allocation * descriptor, u32 offset, u32 size, _maliggy_osk_mem_mapregion_flags_t flags);

} maliggy_kernel_mem_address_manager;

maliggy_allocation_engine maliggy_allocation_engine_create(maliggy_kernel_mem_address_manager * maliggy_address_manager, maliggy_kernel_mem_address_manager * process_address_manager);

void maliggy_allocation_engine_destroy(maliggy_allocation_engine engine);

int maliggy_allocation_engine_allocate_memory(maliggy_allocation_engine engine, maliggy_memory_allocation * descriptor, maliggy_physical_memory_allocator * physical_provider, _maliggy_osk_list_t *tracking_list );
void maliggy_allocation_engine_release_memory(maliggy_allocation_engine engine, maliggy_memory_allocation * descriptor);

void maliggy_allocation_engine_release_pt1_maliggy_pagetables_unmap(maliggy_allocation_engine engine, maliggy_memory_allocation * descriptor);
void maliggy_allocation_engine_release_pt2_physical_memory_free(maliggy_allocation_engine engine, maliggy_memory_allocation * descriptor);

int maliggy_allocation_engine_map_physical(maliggy_allocation_engine engine, maliggy_memory_allocation * descriptor, u32 offset, u32 phys, u32 cpu_usage_adjust, u32 size);
void maliggy_allocation_engine_unmap_physical(maliggy_allocation_engine engine, maliggy_memory_allocation * descriptor, u32 offset, u32 size, _maliggy_osk_mem_mapregion_flags_t unmap_flags);

int maliggy_allocation_engine_allocate_page_tables(maliggy_allocation_engine, maliggy_page_table_block * descriptor, maliggy_physical_memory_allocator * physical_provider);

void maliggy_allocation_engine_report_allocators(maliggy_physical_memory_allocator * physical_provider);

u32 maliggy_allocation_engine_memory_usage(maliggy_physical_memory_allocator *allocator);

#endif /* __MALI_KERNEL_MEMORY_ENGINE_H__ */
