/**
 * Copyright (C) 2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "mali_osk.h"
#include "mali_kernel_common.h"
#include "mali_uk_types.h"
#include "mali_user_settings_db.h"
#include "mali_session.h"

static u32 maliggy_user_settings[_MALI_UK_USER_SETTING_MAX];
const char *_maliggy_uk_user_setting_descriptions[] = _MALI_UK_USER_SETTING_DESCRIPTIONS;

static void maliggy_user_settings_notify(_maliggy_uk_user_setting_t setting, u32 value)
{
	struct maliggy_session_data *session, *tmp;

	maliggy_session_lock();
	MALI_SESSION_FOREACH(session, tmp, link)
	{
		_maliggy_osk_notification_t *notobj = _maliggy_osk_notification_create(_MALI_NOTIFICATION_SETTINGS_CHANGED, sizeof(_maliggy_uk_settings_changed_s));
		_maliggy_uk_settings_changed_s *data = notobj->result_buffer;
		data->setting = setting;
		data->value = value;

		maliggy_session_send_notification(session, notobj);
	}
	maliggy_session_unlock();
}

void maliggy_set_user_setting(_maliggy_uk_user_setting_t setting, u32 value)
{
	maliggy_bool notify = MALI_FALSE;

	if (setting >= _MALI_UK_USER_SETTING_MAX || setting < 0)
	{
		MALI_DEBUG_PRINT_ERROR(("Invalid user setting %ud\n"));
		return;
	}

	if (maliggy_user_settings[setting] != value)
	{
		notify = MALI_TRUE;
	}

	maliggy_user_settings[setting] = value;

	if (notify)
	{
		maliggy_user_settings_notify(setting, value);
	}
}

u32 maliggy_get_user_setting(_maliggy_uk_user_setting_t setting)
{
	if (setting >= _MALI_UK_USER_SETTING_MAX || setting < 0)
	{
		return 0;
	}

	return maliggy_user_settings[setting];
}

_maliggy_osk_errcode_t _maliggy_ukk_get_user_setting(_maliggy_uk_get_user_setting_s *args)
{
	_maliggy_uk_user_setting_t setting;
	MALI_DEBUG_ASSERT_POINTER(args);

	setting = args->setting;

	if (0 <= setting && _MALI_UK_USER_SETTING_MAX > setting)
	{
		args->value = maliggy_user_settings[setting];
		return _MALI_OSK_ERR_OK;
	}
	else
	{
		return _MALI_OSK_ERR_INVALID_ARGS;
	}
}

_maliggy_osk_errcode_t _maliggy_ukk_get_user_settings(_maliggy_uk_get_user_settings_s *args)
{
	MALI_DEBUG_ASSERT_POINTER(args);

	_maliggy_osk_memcpy(args->settings, maliggy_user_settings, sizeof(maliggy_user_settings));

	return _MALI_OSK_ERR_OK;
}
