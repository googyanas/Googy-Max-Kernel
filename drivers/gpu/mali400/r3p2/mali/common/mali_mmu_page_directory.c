/*
 * Copyright (C) 2011-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "mali_kernel_common.h"
#include "mali_kernel_core.h"
#include "mali_osk.h"
#include "mali_uk_types.h"
#include "mali_mmu_page_directory.h"
#include "mali_memory.h"
#include "mali_l2_cache.h"
#include "mali_group.h"

static _maliggy_osk_errcode_t fill_page(maliggy_io_address mapping, u32 data);

u32 maliggy_allocate_empty_page(void)
{
	_maliggy_osk_errcode_t err;
	maliggy_io_address mapping;
	u32 address;

	if(_MALI_OSK_ERR_OK != maliggy_mmu_get_table_page(&address, &mapping))
	{
		/* Allocation failed */
		return 0;
	}

	MALI_DEBUG_ASSERT_POINTER( mapping );

	err = fill_page(mapping, 0);
	if (_MALI_OSK_ERR_OK != err)
	{
		maliggy_mmu_release_table_page(address);
	}
	return address;
}

void maliggy_free_empty_page(u32 address)
{
	if (MALI_INVALID_PAGE != address)
	{
		maliggy_mmu_release_table_page(address);
	}
}

_maliggy_osk_errcode_t maliggy_create_fault_flush_pages(u32 *page_directory, u32 *page_table, u32 *data_page)
{
	_maliggy_osk_errcode_t err;
	maliggy_io_address page_directory_mapping;
	maliggy_io_address page_table_mapping;
	maliggy_io_address data_page_mapping;

	err = maliggy_mmu_get_table_page(data_page, &data_page_mapping);
	if (_MALI_OSK_ERR_OK == err)
	{
		err = maliggy_mmu_get_table_page(page_table, &page_table_mapping);
		if (_MALI_OSK_ERR_OK == err)
		{
			err = maliggy_mmu_get_table_page(page_directory, &page_directory_mapping);
			if (_MALI_OSK_ERR_OK == err)
			{
				fill_page(data_page_mapping, 0);
				fill_page(page_table_mapping, *data_page | MALI_MMU_FLAGS_WRITE_PERMISSION | MALI_MMU_FLAGS_READ_PERMISSION | MALI_MMU_FLAGS_PRESENT);
				fill_page(page_directory_mapping, *page_table | MALI_MMU_FLAGS_PRESENT);
				MALI_SUCCESS;
			}
			maliggy_mmu_release_table_page(*page_table);
			*page_table = MALI_INVALID_PAGE;
		}
		maliggy_mmu_release_table_page(*data_page);
		*data_page = MALI_INVALID_PAGE;
	}
	return err;
}

void maliggy_destroy_fault_flush_pages(u32 *page_directory, u32 *page_table, u32 *data_page)
{
	if (MALI_INVALID_PAGE != *page_directory)
	{
		maliggy_mmu_release_table_page(*page_directory);
		*page_directory = MALI_INVALID_PAGE;
	}

	if (MALI_INVALID_PAGE != *page_table)
	{
		maliggy_mmu_release_table_page(*page_table);
		*page_table = MALI_INVALID_PAGE;
	}

	if (MALI_INVALID_PAGE != *data_page)
	{
		maliggy_mmu_release_table_page(*data_page);
		*data_page = MALI_INVALID_PAGE;
	}
}

static _maliggy_osk_errcode_t fill_page(maliggy_io_address mapping, u32 data)
{
	int i;
	MALI_DEBUG_ASSERT_POINTER( mapping );

	for(i = 0; i < MALI_MMU_PAGE_SIZE/4; i++)
	{
		_maliggy_osk_mem_iowrite32_relaxed( mapping, i * sizeof(u32), data);
	}
	_maliggy_osk_mem_barrier();
	MALI_SUCCESS;
}

_maliggy_osk_errcode_t maliggy_mmu_pagedir_map(struct maliggy_page_directory *pagedir, u32 maliggy_address, u32 size)
{
	const int first_pde = MALI_MMU_PDE_ENTRY(maliggy_address);
	const int last_pde = MALI_MMU_PDE_ENTRY(maliggy_address + size - 1);
	_maliggy_osk_errcode_t err;
	maliggy_io_address pde_mapping;
	u32 pde_phys;
	int i;

	for(i = first_pde; i <= last_pde; i++)
	{
		if(0 == (_maliggy_osk_mem_ioread32(pagedir->page_directory_mapped, i*sizeof(u32)) & MALI_MMU_FLAGS_PRESENT))
		{
			/* Page table not present */
			MALI_DEBUG_ASSERT(0 == pagedir->page_entries_usage_count[i]);
			MALI_DEBUG_ASSERT(NULL == pagedir->page_entries_mapped[i]);

			err = maliggy_mmu_get_table_page(&pde_phys, &pde_mapping);
			if(_MALI_OSK_ERR_OK != err)
			{
				MALI_PRINT_ERROR(("Failed to allocate page table page.\n"));
				return err;
			}
			pagedir->page_entries_mapped[i] = pde_mapping;

			/* Update PDE, mark as present */
			_maliggy_osk_mem_iowrite32_relaxed(pagedir->page_directory_mapped, i*sizeof(u32),
			                pde_phys | MALI_MMU_FLAGS_PRESENT);

			MALI_DEBUG_ASSERT(0 == pagedir->page_entries_usage_count[i]);
			pagedir->page_entries_usage_count[i] = 1;
		}
		else
		{
			pagedir->page_entries_usage_count[i]++;
		}
	}
	_maliggy_osk_write_mem_barrier();

	MALI_SUCCESS;
}

MALI_STATIC_INLINE void maliggy_mmu_zero_pte(maliggy_io_address page_table, u32 maliggy_address, u32 size)
{
	int i;
	const int first_pte = MALI_MMU_PTE_ENTRY(maliggy_address);
	const int last_pte = MALI_MMU_PTE_ENTRY(maliggy_address + size - 1);

	for (i = first_pte; i <= last_pte; i++)
	{
		_maliggy_osk_mem_iowrite32_relaxed(page_table, i * sizeof(u32), 0);
	}
}

_maliggy_osk_errcode_t maliggy_mmu_pagedir_unmap(struct maliggy_page_directory *pagedir, u32 maliggy_address, u32 size)
{
	const int first_pde = MALI_MMU_PDE_ENTRY(maliggy_address);
	const int last_pde = MALI_MMU_PDE_ENTRY(maliggy_address + size - 1);
	u32 left = size;
	int i;
	maliggy_bool pd_changed = MALI_FALSE;
	u32 pages_to_invalidate[3]; /* hard-coded to 3: max two pages from the PT level plus max one page from PD level */
	u32 num_pages_inv = 0;
	maliggy_bool invalidate_all = MALI_FALSE; /* safety mechanism in case page_entries_usage_count is unreliable */

	/* For all page directory entries in range. */
	for (i = first_pde; i <= last_pde; i++)
	{
		u32 size_in_pde, offset;

		MALI_DEBUG_ASSERT_POINTER(pagedir->page_entries_mapped[i]);
		MALI_DEBUG_ASSERT(0 != pagedir->page_entries_usage_count[i]);

		/* Offset into page table, 0 if maliggy_address is 4MiB aligned */
		offset = (maliggy_address & (MALI_MMU_VIRTUAL_PAGE_SIZE - 1));
		if (left < MALI_MMU_VIRTUAL_PAGE_SIZE - offset)
		{
			size_in_pde = left;
		}
		else
		{
			size_in_pde = MALI_MMU_VIRTUAL_PAGE_SIZE - offset;
		}

		pagedir->page_entries_usage_count[i]--;

		/* If entire page table is unused, free it */
		if (0 == pagedir->page_entries_usage_count[i])
		{
			u32 page_address;
			MALI_DEBUG_PRINT(4, ("Releasing page table as this is the last reference\n"));
			/* last reference removed, no need to zero out each PTE  */

			page_address = MALI_MMU_ENTRY_ADDRESS(_maliggy_osk_mem_ioread32(pagedir->page_directory_mapped, i*sizeof(u32)));
			pagedir->page_entries_mapped[i] = NULL;
			_maliggy_osk_mem_iowrite32_relaxed(pagedir->page_directory_mapped, i*sizeof(u32), 0);

			maliggy_mmu_release_table_page(page_address);
			pd_changed = MALI_TRUE;
		}
		else
		{
			MALI_DEBUG_ASSERT(num_pages_inv < 2);
			if (num_pages_inv < 2)
			{
				pages_to_invalidate[num_pages_inv] = maliggy_page_directory_get_phys_address(pagedir, i);
				num_pages_inv++;
			}
			else
			{
				invalidate_all = MALI_TRUE;
			}

			/* If part of the page table is still in use, zero the relevant PTEs */
			maliggy_mmu_zero_pte(pagedir->page_entries_mapped[i], maliggy_address, size_in_pde);
		}

		left -= size_in_pde;
		maliggy_address += size_in_pde;
	}
	_maliggy_osk_write_mem_barrier();

	/* L2 pages invalidation */
	if (MALI_TRUE == pd_changed)
	{
		MALI_DEBUG_ASSERT(num_pages_inv < 3);
		if (num_pages_inv < 3)
		{
			pages_to_invalidate[num_pages_inv] = pagedir->page_directory;
			num_pages_inv++;
		}
		else
		{
			invalidate_all = MALI_TRUE;
		}
	}

	if (invalidate_all)
	{
		maliggy_l2_cache_invalidate_all();
	}
	else
	{
		maliggy_l2_cache_invalidate_all_pages(pages_to_invalidate, num_pages_inv);
	}

	MALI_SUCCESS;
}

struct maliggy_page_directory *maliggy_mmu_pagedir_alloc(void)
{
	struct maliggy_page_directory *pagedir;

	pagedir = _maliggy_osk_calloc(1, sizeof(struct maliggy_page_directory));
	if(NULL == pagedir)
	{
		return NULL;
	}

	if(_MALI_OSK_ERR_OK != maliggy_mmu_get_table_page(&pagedir->page_directory, &pagedir->page_directory_mapped))
	{
		_maliggy_osk_free(pagedir);
		return NULL;
	}

	/* Zero page directory */
	fill_page(pagedir->page_directory_mapped, 0);

	return pagedir;
}

void maliggy_mmu_pagedir_free(struct maliggy_page_directory *pagedir)
{
	const int num_page_table_entries = sizeof(pagedir->page_entries_mapped) / sizeof(pagedir->page_entries_mapped[0]);
	int i;

	/* Free referenced page tables and zero PDEs. */
	for (i = 0; i < num_page_table_entries; i++)
	{
		if (pagedir->page_directory_mapped && (_maliggy_osk_mem_ioread32(pagedir->page_directory_mapped, sizeof(u32)*i) & MALI_MMU_FLAGS_PRESENT))
		{
			maliggy_mmu_release_table_page( _maliggy_osk_mem_ioread32(pagedir->page_directory_mapped, i*sizeof(u32)) & ~MALI_MMU_FLAGS_MASK);
			_maliggy_osk_mem_iowrite32_relaxed(pagedir->page_directory_mapped, i * sizeof(u32), 0);
		}
	}
	_maliggy_osk_write_mem_barrier();

	/* Free the page directory page. */
	maliggy_mmu_release_table_page(pagedir->page_directory);

	_maliggy_osk_free(pagedir);
}


void maliggy_mmu_pagedir_update(struct maliggy_page_directory *pagedir, u32 maliggy_address, u32 phys_address, u32 size, maliggy_memory_cache_settings cache_settings)
{
	u32 end_address = maliggy_address + size;
	u32 permission_bits;

	switch ( cache_settings )
	{
		case MALI_CACHE_GP_READ_ALLOCATE:
		MALI_DEBUG_PRINT(5, ("Map L2 GP_Read_allocate\n"));
		permission_bits = MALI_MMU_FLAGS_FORCE_GP_READ_ALLOCATE;
		break;

		case MALI_CACHE_STANDARD:
		MALI_DEBUG_PRINT(5, ("Map L2 Standard\n"));
		/*falltrough */
		default:
		if ( MALI_CACHE_STANDARD != cache_settings) MALI_PRINT_ERROR(("Wrong cache settings\n"));
		permission_bits = MALI_MMU_FLAGS_WRITE_PERMISSION | MALI_MMU_FLAGS_READ_PERMISSION | MALI_MMU_FLAGS_PRESENT;
	}

	/* Map physical pages into MMU page tables */
	for ( ; maliggy_address < end_address; maliggy_address += MALI_MMU_PAGE_SIZE, phys_address += MALI_MMU_PAGE_SIZE)
	{
		MALI_DEBUG_ASSERT_POINTER(pagedir->page_entries_mapped[MALI_MMU_PDE_ENTRY(maliggy_address)]);
		_maliggy_osk_mem_iowrite32_relaxed(pagedir->page_entries_mapped[MALI_MMU_PDE_ENTRY(maliggy_address)],
		                MALI_MMU_PTE_ENTRY(maliggy_address) * sizeof(u32),
			        phys_address | permission_bits);
	}
	_maliggy_osk_write_mem_barrier();
}

u32 maliggy_page_directory_get_phys_address(struct maliggy_page_directory *pagedir, u32 index)
{
	return (_maliggy_osk_mem_ioread32(pagedir->page_directory_mapped, index*sizeof(u32)) & ~MALI_MMU_FLAGS_MASK);
}

/* For instrumented */
struct dumpggy_info
{
	u32 buffer_left;
	u32 register_writes_size;
	u32 page_table_dumpggy_size;
	u32 *buffer;
};

static _maliggy_osk_errcode_t writereg(u32 where, u32 what, const char *comment, struct dumpggy_info *info)
{
	if (NULL != info)
	{
		info->register_writes_size += sizeof(u32)*2; /* two 32-bit words */

		if (NULL != info->buffer)
		{
			/* check that we have enough space */
			if (info->buffer_left < sizeof(u32)*2) MALI_ERROR(_MALI_OSK_ERR_NOMEM);

			*info->buffer = where;
			info->buffer++;

			*info->buffer = what;
			info->buffer++;

			info->buffer_left -= sizeof(u32)*2;
		}
	}

	MALI_SUCCESS;
}

static _maliggy_osk_errcode_t dumpggy_page(maliggy_io_address page, u32 phys_addr, struct dumpggy_info * info)
{
	if (NULL != info)
	{
		/* 4096 for the page and 4 bytes for the address */
		const u32 page_size_in_elements = MALI_MMU_PAGE_SIZE / 4;
		const u32 page_size_in_bytes = MALI_MMU_PAGE_SIZE;
		const u32 dumpggy_size_in_bytes = MALI_MMU_PAGE_SIZE + 4;

		info->page_table_dumpggy_size += dumpggy_size_in_bytes;

		if (NULL != info->buffer)
		{
			if (info->buffer_left < dumpggy_size_in_bytes) MALI_ERROR(_MALI_OSK_ERR_NOMEM);

			*info->buffer = phys_addr;
			info->buffer++;

			_maliggy_osk_memcpy(info->buffer, page, page_size_in_bytes);
			info->buffer += page_size_in_elements;

			info->buffer_left -= dumpggy_size_in_bytes;
		}
	}

	MALI_SUCCESS;
}

static _maliggy_osk_errcode_t dumpggy_mmu_page_table(struct maliggy_page_directory *pagedir, struct dumpggy_info * info)
{
	MALI_DEBUG_ASSERT_POINTER(pagedir);
	MALI_DEBUG_ASSERT_POINTER(info);

	if (NULL != pagedir->page_directory_mapped)
	{
		int i;

		MALI_CHECK_NO_ERROR(
			dumpggy_page(pagedir->page_directory_mapped, pagedir->page_directory, info)
			);

		for (i = 0; i < 1024; i++)
		{
			if (NULL != pagedir->page_entries_mapped[i])
			{
				MALI_CHECK_NO_ERROR(
				    dumpggy_page(pagedir->page_entries_mapped[i],
				        _maliggy_osk_mem_ioread32(pagedir->page_directory_mapped,
				        i * sizeof(u32)) & ~MALI_MMU_FLAGS_MASK, info)
				);
			}
		}
	}

	MALI_SUCCESS;
}

static _maliggy_osk_errcode_t dumpggy_mmu_registers(struct maliggy_page_directory *pagedir, struct dumpggy_info * info)
{
	MALI_CHECK_NO_ERROR(writereg(0x00000000, pagedir->page_directory,
	                             "set the page directory address", info));
	MALI_CHECK_NO_ERROR(writereg(0x00000008, 4, "zap???", info));
	MALI_CHECK_NO_ERROR(writereg(0x00000008, 0, "enable paging", info));
	MALI_SUCCESS;
}

_maliggy_osk_errcode_t _maliggy_ukk_query_mmu_page_table_dumpggy_size( _maliggy_uk_query_mmu_page_table_dumpggy_size_s *args )
{
	struct dumpggy_info info = { 0, 0, 0, NULL };
	struct maliggy_session_data * session_data;

	MALI_DEBUG_ASSERT_POINTER(args);
  	MALI_CHECK_NON_NULL(args->ctx, _MALI_OSK_ERR_INVALID_ARGS);

	session_data = (struct maliggy_session_data *)(args->ctx);

	MALI_CHECK_NO_ERROR(dumpggy_mmu_registers(session_data->page_directory, &info));
	MALI_CHECK_NO_ERROR(dumpggy_mmu_page_table(session_data->page_directory, &info));
	args->size = info.register_writes_size + info.page_table_dumpggy_size;
	MALI_SUCCESS;
}

_maliggy_osk_errcode_t _maliggy_ukk_dumpggy_mmu_page_table( _maliggy_uk_dumpggy_mmu_page_table_s * args )
{
	struct dumpggy_info info = { 0, 0, 0, NULL };
	struct maliggy_session_data * session_data;

  	MALI_DEBUG_ASSERT_POINTER(args);
  	MALI_CHECK_NON_NULL(args->ctx, _MALI_OSK_ERR_INVALID_ARGS);
	MALI_CHECK_NON_NULL(args->buffer, _MALI_OSK_ERR_INVALID_ARGS);

	session_data = (struct maliggy_session_data *)(args->ctx);

	info.buffer_left = args->size;
	info.buffer = args->buffer;

	args->register_writes = info.buffer;
	MALI_CHECK_NO_ERROR(dumpggy_mmu_registers(session_data->page_directory, &info));

	args->page_table_dump = info.buffer;
	MALI_CHECK_NO_ERROR(dumpggy_mmu_page_table(session_data->page_directory, &info));

	args->register_writes_size = info.register_writes_size;
	args->page_table_dumpggy_size = info.page_table_dumpggy_size;

	MALI_SUCCESS;
}
