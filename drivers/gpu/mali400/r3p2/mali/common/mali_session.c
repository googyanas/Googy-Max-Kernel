/*
 * Copyright (C) 2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "mali_osk.h"
#include "mali_osk_list.h"
#include "mali_session.h"

_MALI_OSK_LIST_HEAD(maliggy_sessions);

_maliggy_osk_lock_t *maliggy_sessions_lock;

_maliggy_osk_errcode_t maliggy_session_initialize(void)
{
	_MALI_OSK_INIT_LIST_HEAD(&maliggy_sessions);

	maliggy_sessions_lock = _maliggy_osk_lock_init(_MALI_OSK_LOCKFLAG_READERWRITER | _MALI_OSK_LOCKFLAG_ORDERED, 0, _MALI_OSK_LOCK_ORDER_SESSIONS);

	if (NULL == maliggy_sessions_lock) return _MALI_OSK_ERR_NOMEM;

	return _MALI_OSK_ERR_OK;
}

void maliggy_session_terminate(void)
{
	_maliggy_osk_lock_term(maliggy_sessions_lock);
}

void maliggy_session_add(struct maliggy_session_data *session)
{
	maliggy_session_lock();
	_maliggy_osk_list_add(&session->link, &maliggy_sessions);
	maliggy_session_unlock();
}

void maliggy_session_remove(struct maliggy_session_data *session)
{
	maliggy_session_lock();
	_maliggy_osk_list_delinit(&session->link);
	maliggy_session_unlock();
}
