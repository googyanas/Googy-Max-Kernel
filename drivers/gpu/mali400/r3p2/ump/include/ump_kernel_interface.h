/*
 * Copyright (C) 2010, 2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file umpggy_kernel_interface.h
 *
 * This file contains the kernel space part of the UMP API.
 */

#ifndef __UMP_KERNEL_INTERFACE_H__
#define __UMP_KERNEL_INTERFACE_H__


/** @defgroup umpggy_kernel_space_api UMP Kernel Space API
 * @{ */


#include "ump_kernel_platform.h"


#ifdef __cplusplus
extern "C"
{
#endif


/**
 * External representation of a UMP handle in kernel space.
 */
typedef void * umpggy_dd_handle;

/**
 * Typedef for a secure ID, a system wide identificator for UMP memory buffers.
 */
typedef unsigned int umpggy_secure_id;


/**
 * Value to indicate an invalid UMP memory handle.
 */
#define UMP_DD_HANDLE_INVALID ((umpggy_dd_handle)0)


/**
 * Value to indicate an invalid secure Id.
 */
#define UMP_INVALID_SECURE_ID ((umpggy_secure_id)-1)


/**
 * UMP error codes for kernel space.
 */
typedef enum
{
	UMP_DD_SUCCESS, /**< indicates success */
	UMP_DD_INVALID, /**< indicates failure */
} umpggy_dd_status_code;


/**
 * Struct used to describe a physical block used by UMP memory
 */
typedef struct umpggy_dd_physical_block
{
	unsigned long addr; /**< The physical address of the block */
	unsigned long size; /**< The length of the block, typically page aligned */
} umpggy_dd_physical_block;


/**
 * Retrieves the secure ID for the specified UMP memory.
 *
 * This identificator is unique across the entire system, and uniquely identifies
 * the specified UMP memory. This identificator can later be used through the
 * @ref umpggy_dd_handle_create_from_secure_id "ump_dd_handle_create_from_secure_id" or
 * @ref umpggy_handle_create_from_secure_id "ump_handle_create_from_secure_id"
 * functions in order to access this UMP memory, for instance from another process.
 *
 * @note There is a user space equivalent function called @ref umpggy_secure_id_get "ump_secure_id_get"
 *
 * @see umpggy_dd_handle_create_from_secure_id
 * @see umpggy_handle_create_from_secure_id
 * @see umpggy_secure_id_get
 *
 * @param mem Handle to UMP memory.
 *
 * @return Returns the secure ID for the specified UMP memory.
 */
UMP_KERNEL_API_EXPORT umpggy_secure_id umpggy_dd_secure_id_get(umpggy_dd_handle mem);


/**
 * Retrieves a handle to allocated UMP memory.
 *
 * The usage of UMP memory is reference counted, so this will increment the reference
 * count by one for the specified UMP memory.
 * Use @ref umpggy_dd_reference_release "ump_dd_reference_release" when there is no longer any
 * use for the retrieved handle.
 *
 * @note There is a user space equivalent function called @ref umpggy_handle_create_from_secure_id "ump_handle_create_from_secure_id"
 *
 * @see umpggy_dd_reference_release
 * @see umpggy_handle_create_from_secure_id
 *
 * @param secure_id The secure ID of the UMP memory to open, that can be retrieved using the @ref umpggy_secure_id_get "ump_secure_id_get " function.
 *
 * @return UMP_INVALID_MEMORY_HANDLE indicates failure, otherwise a valid handle is returned.
 */
UMP_KERNEL_API_EXPORT umpggy_dd_handle umpggy_dd_handle_create_from_secure_id(umpggy_secure_id secure_id);


/**
 * Retrieves the number of physical blocks used by the specified UMP memory.
 *
 * This function retrieves the number of @ref umpggy_dd_physical_block "ump_dd_physical_block" structs needed
 * to describe the physical memory layout of the given UMP memory. This can later be used when calling
 * the functions @ref umpggy_dd_phys_blocks_get "ump_dd_phys_blocks_get" and
 * @ref umpggy_dd_phys_block_get "ump_dd_phys_block_get".
 *
 * @see umpggy_dd_phys_blocks_get
 * @see umpggy_dd_phys_block_get
 *
 * @param mem Handle to UMP memory.
 *
 * @return The number of umpggy_dd_physical_block structs required to describe the physical memory layout of the specified UMP memory.
 */
UMP_KERNEL_API_EXPORT unsigned long umpggy_dd_phys_block_count_get(umpggy_dd_handle mem);


/**
 * Retrieves all physical memory block information for specified UMP memory.
 *
 * This function can be used by other device drivers in order to create MMU tables.
 *
 * @note This function will fail if the num_blocks parameter is either to large or to small.
 *
 * @see umpggy_dd_phys_block_get
 *
 * @param mem Handle to UMP memory.
 * @param blocks An array of @ref umpggy_dd_physical_block "ump_dd_physical_block" structs that will receive the physical description.
 * @param num_blocks The number of blocks to return in the blocks array. Use the function
 *                   @ref umpggy_dd_phys_block_count_get "ump_dd_phys_block_count_get" first to determine the number of blocks required.
 *
 * @return UMP_DD_SUCCESS indicates success, UMP_DD_INVALID indicates failure.
 */
UMP_KERNEL_API_EXPORT umpggy_dd_status_code umpggy_dd_phys_blocks_get(umpggy_dd_handle mem, umpggy_dd_physical_block * blocks, unsigned long num_blocks);


/**
 * Retrieves the physical memory block information for specified block for the specified UMP memory.
 *
 * This function can be used by other device drivers in order to create MMU tables.
 *
 * @note This function will return UMP_DD_INVALID if the specified index is out of range.
 *
 * @see umpggy_dd_phys_blocks_get
 *
 * @param mem Handle to UMP memory.
 * @param index Which physical info block to retrieve.
 * @param block Pointer to a @ref umpggy_dd_physical_block "ump_dd_physical_block" struct which will receive the requested information.
 *
 * @return UMP_DD_SUCCESS indicates success, UMP_DD_INVALID indicates failure.
 */
UMP_KERNEL_API_EXPORT umpggy_dd_status_code umpggy_dd_phys_block_get(umpggy_dd_handle mem, unsigned long index, umpggy_dd_physical_block * block);


/**
 * Retrieves the actual size of the specified UMP memory.
 *
 * The size is reported in bytes, and is typically page aligned.
 *
 * @note There is a user space equivalent function called @ref umpggy_size_get "ump_size_get"
 *
 * @see umpggy_size_get
 *
 * @param mem Handle to UMP memory.
 *
 * @return Returns the allocated size of the specified UMP memory, in bytes.
 */
UMP_KERNEL_API_EXPORT unsigned long umpggy_dd_size_get(umpggy_dd_handle mem);


/**
 * Adds an extra reference to the specified UMP memory.
 *
 * This function adds an extra reference to the specified UMP memory. This function should
 * be used every time a UMP memory handle is duplicated, that is, assigned to another umpggy_dd_handle
 * variable. The function @ref umpggy_dd_reference_release "ump_dd_reference_release" must then be used
 * to release each copy of the UMP memory handle.
 *
 * @note You are not required to call @ref umpggy_dd_reference_add "ump_dd_reference_add"
 * for UMP handles returned from
 * @ref umpggy_dd_handle_create_from_secure_id "ump_dd_handle_create_from_secure_id",
 * because these handles are already reference counted by this function.
 *
 * @note There is a user space equivalent function called @ref umpggy_reference_add "ump_reference_add"
 *
 * @see umpggy_reference_add
 *
 * @param mem Handle to UMP memory.
 */
UMP_KERNEL_API_EXPORT void umpggy_dd_reference_add(umpggy_dd_handle mem);


/**
 * Releases a reference from the specified UMP memory.
 *
 * This function should be called once for every reference to the UMP memory handle.
 * When the last reference is released, all resources associated with this UMP memory
 * handle are freed.
 *
 * @note There is a user space equivalent function called @ref umpggy_reference_release "ump_reference_release"
 *
 * @see umpggy_reference_release
 *
 * @param mem Handle to UMP memory.
 */
UMP_KERNEL_API_EXPORT void umpggy_dd_reference_release(umpggy_dd_handle mem);


#ifdef __cplusplus
}
#endif


/** @} */ /* end group umpggy_kernel_space_api */


#endif  /* __UMP_KERNEL_INTERFACE_H__ */
