/*
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/module.h>            /* kernel module definitions */
#include <linux/fs.h>                /* file system operations */
#include <linux/cdev.h>              /* character device definitions */
#include <linux/ioport.h>            /* request_mem_region */
#include <linux/mm.h>                /* memory management functions and types */
#include <asm/uaccess.h>             /* user space access */
#include <asm/atomic.h>
#include <linux/device.h>
#include <linux/debugfs.h>

#include "arch/config.h"             /* Configuration for current platform. The symlinc for arch is set by Makefile */
#include "ump_ioctl.h"
#include "ump_kernel_common.h"
#include "ump_kernel_interface.h"
#include "ump_kernel_interface_ref_drv.h"
#include "ump_kernel_descriptor_mapping.h"
#include "ump_kernel_memory_backend.h"
#include "ump_kernel_memory_backend_os.h"
#include "ump_kernel_memory_backend_dedicated.h"
#include "ump_kernel_license.h"

#include "ump_osk.h"
#include "ump_ukk.h"
#include "ump_uk_types.h"
#include "ump_ukk_wrappers.h"
#include "ump_ukk_ref_wrappers.h"

/* MALI_SEC */
#ifdef CONFIG_ION_EXYNOS
#include <linux/ion.h>
extern struct ion_device *ion_exynos;
struct ion_client *ion_client_ump_ggy_ggy = NULL;
#endif

/* MALI_SEC */
#if defined(CONFIG_MALI400)
extern int map_errcode_ggy_ggy( _maliggy_osk_errcode_t err );
#endif

/* Module parameter to control log level */
int umpggy_debug_level = 2;
module_param(umpggy_debug_level, int, S_IRUSR | S_IWUSR | S_IWGRP | S_IRGRP | S_IROTH); /* rw-rw-r-- */
MODULE_PARM_DESC(umpggy_debug_level, "Higher number, more dmesg output");

/* By default the module uses any available major, but it's possible to set it at load time to a specific number */
int umpggy_major = 0;
module_param(umpggy_major, int, S_IRUGO); /* r--r--r-- */
MODULE_PARM_DESC(umpggy_major, "Device major number");

/* Name of the UMP device driver */
static char umpggy_dev_name[] = "ump"; /* should be const, but the functions we call requires non-cost */


#if UMP_LICENSE_IS_GPL
static struct dentry *umpggy_debugfs_dir = NULL;
#endif

/*
 * The data which we attached to each virtual memory mapping request we get.
 * Each memory mapping has a reference to the UMP memory it maps.
 * We release this reference when the last memory mapping is unmapped.
 */
typedef struct umpggy_vma_usage_tracker
{
	int references;
	umpggy_dd_handle handle;
} umpggy_vma_usage_tracker;

struct umpggy_device
{
	struct cdev cdev;
#if UMP_LICENSE_IS_GPL
	struct class * umpggy_class;
#endif
};

/* The global variable containing the global device data */
static struct umpggy_device umpggy_device;


/* Forward declare static functions */
static int umpggy_file_open(struct inode *inode, struct file *filp);
static int umpggy_file_release(struct inode *inode, struct file *filp);
#ifdef HAVE_UNLOCKED_IOCTL
static long umpggy_file_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
#else
static int umpggy_file_ioctl(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg);
#endif
static int umpggy_file_mmap(struct file * filp, struct vm_area_struct * vma);


/* This variable defines the file operations this UMP device driver offer */
static struct file_operations umpggy_fops =
{
	.owner   = THIS_MODULE,
	.open    = umpggy_file_open,
	.release = umpggy_file_release,
#ifdef HAVE_UNLOCKED_IOCTL
	.unlocked_ioctl   = umpggy_file_ioctl,
#else
	.ioctl   = umpggy_file_ioctl,
#endif
	.mmap    = umpggy_file_mmap
};


/* This function is called by Linux to initialize this module.
 * All we do is initialize the UMP device driver.
 */
static int umpggy_initialize_module(void)
{
	_maliggy_osk_errcode_t err;

	DBG_MSG(2, ("Inserting UMP device driver. Compiled: %s, time: %s\n", __DATE__, __TIME__));

	err = umpggy_kernel_constructor();
	if (_MALI_OSK_ERR_OK != err)
	{
		MSG_ERR(("UMP device driver init failed\n"));
		return map_errcode_ggy_ggy(err);
	}

	MSG(("UMP device driver %s loaded\n", SVN_REV_STRING));
	return 0;
}



/*
 * This function is called by Linux to unload/terminate/exit/cleanup this module.
 * All we do is terminate the UMP device driver.
 */
static void umpggy_cleanup_module(void)
{
/* MALI_SEC */
#ifdef CONFIG_ION_EXYNOS
	if (ion_client_ump_ggy_ggy)
	    ion_client_destroy(ion_client_ump_ggy_ggy);
#endif

	DBG_MSG(2, ("Unloading UMP device driver\n"));
	umpggy_kernel_destructor();
	DBG_MSG(2, ("Module unloaded\n"));
}



static ssize_t umpggy_memory_used_read(struct file *filp, char __user *ubuf, size_t cnt, loff_t *ppos)
{
        char buf[64];
        size_t r;
        u32 mem = _umpggy_ukk_report_memory_usage();

        r = snprintf(buf, 64, "%u\n", mem);
        return simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
}

static const struct file_operations umpggy_memory_usage_fops = {
        .owner = THIS_MODULE,
        .read = umpggy_memory_used_read,
};

/*
 * Initialize the UMP device driver.
 */
int umpggy_kernel_device_initialize(void)
{
	int err;
	dev_t dev = 0;
#if UMP_LICENSE_IS_GPL
	umpggy_debugfs_dir = debugfs_create_dir(umpggy_dev_name, NULL);
	if (ERR_PTR(-ENODEV) == umpggy_debugfs_dir)
	{
			umpggy_debugfs_dir = NULL;
	}
	else
	{
		debugfs_create_file("memory_usage", 0400, umpggy_debugfs_dir, NULL, &umpggy_memory_usage_fops);
	}
#endif

	if (0 == umpggy_major)
	{
		/* auto select a major */
		err = alloc_chrdev_region(&dev, 0, 1, umpggy_dev_name);
		umpggy_major = MAJOR(dev);
	}
	else
	{
		/* use load time defined major number */
		dev = MKDEV(umpggy_major, 0);
		err = register_chrdev_region(dev, 1, umpggy_dev_name);
	}

	if (0 == err)
	{
		memset(&umpggy_device, 0, sizeof(umpggy_device));

		/* initialize our char dev data */
		cdev_init(&umpggy_device.cdev, &umpggy_fops);
		umpggy_device.cdev.owner = THIS_MODULE;
		umpggy_device.cdev.ops = &umpggy_fops;

		/* register char dev with the kernel */
		err = cdev_add(&umpggy_device.cdev, dev, 1/*count*/);
		if (0 == err)
		{

#if UMP_LICENSE_IS_GPL
			umpggy_device.umpggy_class = class_create(THIS_MODULE, umpggy_dev_name);
			if (IS_ERR(umpggy_device.umpggy_class))
			{
				err = PTR_ERR(umpggy_device.umpggy_class);
			}
			else
			{
				struct device * mdev;
				mdev = device_create(umpggy_device.umpggy_class, NULL, dev, NULL, umpggy_dev_name);
				if (!IS_ERR(mdev))
				{
					return 0;
				}

				err = PTR_ERR(mdev);
			}
			cdev_del(&umpggy_device.cdev);
#else
			return 0;
#endif
		}

		unregister_chrdev_region(dev, 1);
	}

	return err;
}



/*
 * Terminate the UMP device driver
 */
void umpggy_kernel_device_terminate(void)
{
	dev_t dev = MKDEV(umpggy_major, 0);

#if UMP_LICENSE_IS_GPL
	device_destroy(umpggy_device.umpggy_class, dev);
	class_destroy(umpggy_device.umpggy_class);
#endif

	/* unregister char device */
	cdev_del(&umpggy_device.cdev);

	/* free major */
	unregister_chrdev_region(dev, 1);

#if UMP_LICENSE_IS_GPL
	if(umpggy_debugfs_dir)
		debugfs_remove_recursive(umpggy_debugfs_dir);
#endif
}

/*
 * Open a new session. User space has called open() on us.
 */
static int umpggy_file_open(struct inode *inode, struct file *filp)
{
	struct umpggy_session_data * session_data;
	_maliggy_osk_errcode_t err;

	/* input validation */
	if (0 != MINOR(inode->i_rdev))
	{
		MSG_ERR(("Minor not zero in umpggy_file_open()\n"));
		return -ENODEV;
	}

	/* Call the OS-Independent UMP Open function */
	err = _umpggy_ukk_open((void**) &session_data );
	if( _MALI_OSK_ERR_OK != err )
	{
		MSG_ERR(("Ump failed to open a new session\n"));
		return map_errcode_ggy_ggy( err );
	}

	filp->private_data = (void*)session_data;
	filp->f_pos = 0;

	return 0; /* success */
}



/*
 * Close a session. User space has called close() or crashed/terminated.
 */
static int umpggy_file_release(struct inode *inode, struct file *filp)
{
	_maliggy_osk_errcode_t err;

	err = _umpggy_ukk_close((void**) &filp->private_data );
	if( _MALI_OSK_ERR_OK != err )
	{
		return map_errcode_ggy_ggy( err );
	}

	return 0;  /* success */
}



/*
 * Handle IOCTL requests.
 */
#ifdef HAVE_UNLOCKED_IOCTL
static long umpggy_file_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
#else
static int umpggy_file_ioctl(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg)
#endif
{
	int err = -ENOTTY;
	void __user * argument;
	struct umpggy_session_data * session_data;

#ifndef HAVE_UNLOCKED_IOCTL
	(void)inode; /* inode not used */
#endif

	session_data = (struct umpggy_session_data *)filp->private_data;
	if (NULL == session_data)
	{
		MSG_ERR(("No session data attached to file object\n"));
		return -ENOTTY;
	}

	/* interpret the argument as a user pointer to something */
	argument = (void __user *)arg;

	switch (cmd)
	{
		case UMP_IOC_QUERY_API_VERSION:
			err = umpggy_get_api_version_wrapper_ggy_ggy((u32 __user *)argument, session_data);
			break;

		case UMP_IOC_ALLOCATE :
			err = umpggy_allocate_wrapper((u32 __user *)argument, session_data);
			break;
/* MALI_SEC */
#ifdef CONFIG_ION_EXYNOS
		case UMP_IOC_ION_IMPORT:
			err = umpggy_ion_import_wrapper((u32 __user *)argument, session_data);
			break;
#endif
#ifdef CONFIG_DMA_SHARED_BUFFER
		case UMP_IOC_DMABUF_IMPORT:
			err = umpggy_dmabuf_import_wrapper((u32 __user *)argument,
							session_data);
			break;
#endif
		case UMP_IOC_RELEASE:
			err = umpggy_release_wrapper((u32 __user *)argument, session_data);
			break;

		case UMP_IOC_SIZE_GET:
			err = umpggy_size_get_wrapper((u32 __user *)argument, session_data);
			break;

		case UMP_IOC_MSYNC:
			err = umpggy_msync_wrapper((u32 __user *)argument, session_data);
			break;

		case UMP_IOC_CACHE_OPERATIONS_CONTROL:
			err = umpggy_cache_operations_control_wrapper((u32 __user *)argument, session_data);
			break;

		case UMP_IOC_SWITCH_HW_USAGE:
			err = umpggy_switch_hw_usage_wrapper((u32 __user *)argument, session_data);
			break;

		case UMP_IOC_LOCK:
			err = umpggy_lock_wrapper((u32 __user *)argument, session_data);
			break;

		case UMP_IOC_UNLOCK:
			err = umpggy_unlock_wrapper((u32 __user *)argument, session_data);
			break;

		default:
			DBG_MSG(1, ("No handler for IOCTL. cmd: 0x%08x, arg: 0x%08lx\n", cmd, arg));
			err = -EFAULT;
			break;
	}

	return err;
}

/* MALI_SEC */
#if !defined(CONFIG_MALI400)
int map_errcode_ggy_ggy( _maliggy_osk_errcode_t err )
{
    switch(err)
    {
        case _MALI_OSK_ERR_OK : return 0;
        case _MALI_OSK_ERR_FAULT: return -EFAULT;
        case _MALI_OSK_ERR_INVALID_FUNC: return -ENOTTY;
        case _MALI_OSK_ERR_INVALID_ARGS: return -EINVAL;
        case _MALI_OSK_ERR_NOMEM: return -ENOMEM;
        case _MALI_OSK_ERR_TIMEOUT: return -ETIMEDOUT;
        case _MALI_OSK_ERR_RESTARTSYSCALL: return -ERESTARTSYS;
        case _MALI_OSK_ERR_ITEM_NOT_FOUND: return -ENOENT;
        default: return -EFAULT;
    }
}
#endif

/*
 * Handle from OS to map specified virtual memory to specified UMP memory.
 */
static int umpggy_file_mmap(struct file * filp, struct vm_area_struct * vma)
{
	_umpggy_uk_map_mem_s args;
	_maliggy_osk_errcode_t err;
	struct umpggy_session_data * session_data;

	/* Validate the session data */
	session_data = (struct umpggy_session_data *)filp->private_data;
	/* MALI_SEC */
	// original : if (NULL == session_data)
	if (NULL == session_data || NULL == session_data->cookies_map->table->mappings)
	{
		MSG_ERR(("mmap() called without any session data available\n"));
		return -EFAULT;
	}

	/* Re-pack the arguments that mmap() packed for us */
	args.ctx = session_data;
	args.phys_addr = 0;
	args.size = vma->vm_end - vma->vm_start;
	args._ukk_private = vma;
	args.secure_id = vma->vm_pgoff;
	args.is_cached = 0;

	if (!(vma->vm_flags & VM_SHARED))
	{
		args.is_cached = 1;
		vma->vm_flags = vma->vm_flags | VM_SHARED | VM_MAYSHARE  ;
		DBG_MSG(3, ("UMP Map function: Forcing the CPU to use cache\n"));
	}
	/* By setting this flag, during a process fork; the child process will not have the parent UMP mappings */
	vma->vm_flags |= VM_DONTCOPY;

	DBG_MSG(4, ("UMP vma->flags: %x\n", vma->vm_flags ));

	/* Call the common mmap handler */
	err = _umpggy_ukk_map_mem( &args );
	if ( _MALI_OSK_ERR_OK != err)
	{
		MSG_ERR(("_umpggy_ukk_map_mem() failed in function umpggy_file_mmap()"));
		return map_errcode_ggy_ggy( err );
	}

	return 0; /* success */
}

/* Export UMP kernel space API functions */
EXPORT_SYMBOL(umpggy_dd_secure_id_get);
EXPORT_SYMBOL(umpggy_dd_handle_create_from_secure_id);
EXPORT_SYMBOL(umpggy_dd_phys_block_count_get);
EXPORT_SYMBOL(umpggy_dd_phys_block_get);
EXPORT_SYMBOL(umpggy_dd_phys_blocks_get);
EXPORT_SYMBOL(umpggy_dd_size_get);
EXPORT_SYMBOL(umpggy_dd_reference_add);
EXPORT_SYMBOL(umpggy_dd_reference_release);
/* MALI_SEC */
EXPORT_SYMBOL(umpggy_dd_meminfo_get);
EXPORT_SYMBOL(umpggy_dd_meminfo_set);
EXPORT_SYMBOL(umpggy_dd_handle_get_from_vaddr);

/* Export our own extended kernel space allocator */
EXPORT_SYMBOL(umpggy_dd_handle_create_from_phys_blocks);

/* Setup init and exit functions for this module */
module_init(umpggy_initialize_module);
module_exit(umpggy_cleanup_module);

/* And some module informatio */
MODULE_LICENSE(UMP_KERNEL_LINUX_LICENSE);
MODULE_AUTHOR("ARM Ltd.");
MODULE_VERSION(SVN_REV_STRING);
