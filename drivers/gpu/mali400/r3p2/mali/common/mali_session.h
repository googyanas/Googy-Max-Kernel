/*
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_SESSION_H__
#define __MALI_SESSION_H__

#include "mali_mmu_page_directory.h"
#include "mali_kernel_descriptor_mapping.h"
#include "mali_osk.h"
#include "mali_osk_list.h"

struct maliggy_session_data
{
	_maliggy_osk_notification_queue_t * ioctl_queue;

#ifdef CONFIG_SYNC
	_maliggy_osk_list_t pending_jobs;
	_maliggy_osk_lock_t *pending_jobs_lock;
#endif

	_maliggy_osk_lock_t *memory_lock; /**< Lock protecting the vm manipulation */
	maliggy_descriptor_mapping * descriptor_mapping; /**< Mapping between userspace descriptors and our pointers */
	_maliggy_osk_list_t memory_head; /**< Track all the memory allocated in this session, for freeing on abnormal termination */

	struct maliggy_page_directory *page_directory; /**< MMU page directory for this session */

	_MALI_OSK_LIST_HEAD(link); /**< Link for list of all sessions */

	_MALI_OSK_LIST_HEAD(job_list); /**< List of all jobs on this session */
};

_maliggy_osk_errcode_t maliggy_session_initialize(void);
void maliggy_session_terminate(void);

/* List of all sessions. Actual list head in maliggy_kernel_core.c */
extern _maliggy_osk_list_t maliggy_sessions;
/* Lock to protect modification and access to the maliggy_sessions list */
extern _maliggy_osk_lock_t *maliggy_sessions_lock;

MALI_STATIC_INLINE void maliggy_session_lock(void)
{
	_maliggy_osk_lock_wait(maliggy_sessions_lock, _MALI_OSK_LOCKMODE_RW);
}

MALI_STATIC_INLINE void maliggy_session_unlock(void)
{
	_maliggy_osk_lock_signal(maliggy_sessions_lock, _MALI_OSK_LOCKMODE_RW);
}

void maliggy_session_add(struct maliggy_session_data *session);
void maliggy_session_remove(struct maliggy_session_data *session);
#define MALI_SESSION_FOREACH(session, tmp, link) \
	_MALI_OSK_LIST_FOREACHENTRY(session, tmp, &maliggy_sessions, struct maliggy_session_data, link)

MALI_STATIC_INLINE struct maliggy_page_directory *maliggy_session_get_page_directory(struct maliggy_session_data *session)
{
	return session->page_directory;
}

MALI_STATIC_INLINE void maliggy_session_send_notification(struct maliggy_session_data *session, _maliggy_osk_notification_t *object)
{
	_maliggy_osk_notification_queue_send(session->ioctl_queue, object);
}

#endif /* __MALI_SESSION_H__ */
