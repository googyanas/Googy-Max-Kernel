/*
 * Copyright (C) 2010-2011 ARM Limited. All rights reserved.
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
#include "ump_kernel_common.h"
#include "ump_kernel_descriptor_mapping.h"

#define MALI_PAD_INT(x) (((x) + (BITS_PER_LONG - 1)) & ~(BITS_PER_LONG - 1))

/**
 * Allocate a descriptor table capable of holding 'count' mappings
 * @param count Number of mappings in the table
 * @return Pointer to a new table, NULL on error
 */
static umpggy_descriptor_table * descriptor_table_alloc(int count);

/**
 * Free a descriptor table
 * @param table The table to free
 */
static void descriptor_table_free(umpggy_descriptor_table * table);

umpggy_descriptor_mapping * umpggy_descriptor_mapping_create(int init_entries, int max_entries)
{
	umpggy_descriptor_mapping * map = _maliggy_osk_calloc(1, sizeof(umpggy_descriptor_mapping) );

	init_entries = MALI_PAD_INT(init_entries);
	max_entries = MALI_PAD_INT(max_entries);

	if (NULL != map)
	{
		map->table = descriptor_table_alloc(init_entries);
		if (NULL != map->table)
		{
			map->lock = _maliggy_osk_lock_init(_MALI_OSK_LOCKFLAG_NONINTERRUPTABLE | _MALI_OSK_LOCKFLAG_READERWRITER, 0 , 0);
			if ( NULL != map->lock )
			{
				_maliggy_osk_set_nonatomic_bit(0, map->table->usage); /* reserve bit 0 to prevent NULL/zero logic to kick in */
				map->max_nr_mappings_allowed = max_entries;
				map->current_nr_mappings = init_entries;
				return map;
			}
			descriptor_table_free(map->table);
		}
		_maliggy_osk_free(map);
	}
	return NULL;
}

void umpggy_descriptor_mapping_destroy(umpggy_descriptor_mapping * map)
{
	descriptor_table_free(map->table);
	_maliggy_osk_lock_term( map->lock );
	_maliggy_osk_free(map);
}

int umpggy_descriptor_mapping_allocate_mapping(umpggy_descriptor_mapping * map, void * target)
{
 	int descriptor = -1;/*-EFAULT;*/
 	_maliggy_osk_lock_wait(map->lock, _MALI_OSK_LOCKMODE_RW);
 	descriptor = _maliggy_osk_find_first_zero_bit(map->table->usage, map->current_nr_mappings);
	if (descriptor == map->current_nr_mappings)
	{
		int nr_mappings_new;
		/* no free descriptor, try to expand the table */
		umpggy_descriptor_table * new_table;
		umpggy_descriptor_table * old_table = map->table;
		nr_mappings_new= map->current_nr_mappings *2;

		if (map->current_nr_mappings >= map->max_nr_mappings_allowed)
		{
			descriptor = -1;
			goto unlock_and_exit;
		}

		new_table = descriptor_table_alloc(nr_mappings_new);
		if (NULL == new_table)
		{
			descriptor = -1;
			goto unlock_and_exit;
		}

 		_maliggy_osk_memcpy(new_table->usage, old_table->usage, (sizeof(unsigned long)*map->current_nr_mappings) / BITS_PER_LONG);
 		_maliggy_osk_memcpy(new_table->mappings, old_table->mappings, map->current_nr_mappings * sizeof(void*));
		map->table = new_table;
		map->current_nr_mappings = nr_mappings_new;
		descriptor_table_free(old_table);
	}

	/* we have found a valid descriptor, set the value and usage bit */
	_maliggy_osk_set_nonatomic_bit(descriptor, map->table->usage);
	map->table->mappings[descriptor] = target;

unlock_and_exit:
	_maliggy_osk_lock_signal(map->lock, _MALI_OSK_LOCKMODE_RW);
	return descriptor;
}

int umpggy_descriptor_mapping_get(umpggy_descriptor_mapping * map, int descriptor, void** target)
{
 	int result = -1;/*-EFAULT;*/
 	DEBUG_ASSERT(map);
 	_maliggy_osk_lock_wait(map->lock, _MALI_OSK_LOCKMODE_RO);
 	if ( (descriptor >= 0) && (descriptor < map->current_nr_mappings) && _maliggy_osk_test_bit(descriptor, map->table->usage) )
	{
		*target = map->table->mappings[descriptor];
		result = 0;
	}
	else *target = NULL;
	_maliggy_osk_lock_signal(map->lock, _MALI_OSK_LOCKMODE_RO);
	return result;
}

int umpggy_descriptor_mapping_set(umpggy_descriptor_mapping * map, int descriptor, void * target)
{
 	int result = -1;/*-EFAULT;*/
 	_maliggy_osk_lock_wait(map->lock, _MALI_OSK_LOCKMODE_RO);
 	if ( (descriptor >= 0) && (descriptor < map->current_nr_mappings) && _maliggy_osk_test_bit(descriptor, map->table->usage) )
	{
		map->table->mappings[descriptor] = target;
		result = 0;
	}
	_maliggy_osk_lock_signal(map->lock, _MALI_OSK_LOCKMODE_RO);
	return result;
}

void umpggy_descriptor_mapping_free(umpggy_descriptor_mapping * map, int descriptor)
{
 	_maliggy_osk_lock_wait(map->lock, _MALI_OSK_LOCKMODE_RW);
 	if ( (descriptor >= 0) && (descriptor < map->current_nr_mappings) && _maliggy_osk_test_bit(descriptor, map->table->usage) )
	{
		map->table->mappings[descriptor] = NULL;
		_maliggy_osk_clear_nonatomic_bit(descriptor, map->table->usage);
	}
	_maliggy_osk_lock_signal(map->lock, _MALI_OSK_LOCKMODE_RW);
}

static umpggy_descriptor_table * descriptor_table_alloc(int count)
{
	umpggy_descriptor_table * table;

	table = _maliggy_osk_calloc(1, sizeof(umpggy_descriptor_table) + ((sizeof(unsigned long) * count)/BITS_PER_LONG) + (sizeof(void*) * count) );

	if (NULL != table)
	{
		table->usage = (u32*)((u8*)table + sizeof(umpggy_descriptor_table));
		table->mappings = (void**)((u8*)table + sizeof(umpggy_descriptor_table) + ((sizeof(unsigned long) * count)/BITS_PER_LONG));
	}

	return table;
}

static void descriptor_table_free(umpggy_descriptor_table * table)
{
	_maliggy_osk_free(table);
}

