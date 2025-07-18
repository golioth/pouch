/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(setttings_callbacks, LOG_LEVEL_WRN);

#include <errno.h>
#include <stddef.h>

#include "settings.h"

int golioth_settings_receive_one(const struct setting_value *value)
{
    STRUCT_SECTION_FOREACH(golioth_settings_handler, setting)
    {
        if (0 == strcmp(setting->key, value->key))
        {
            if (setting->type != value->type)
            {
                return -EFTYPE;
            }

            switch (setting->type)
            {
                case GOLIOTH_SETTING_VALUE_TYPE_INT:
                    return setting->int_cb(value->int_val, setting->cb_arg);

                case GOLIOTH_SETTING_VALUE_TYPE_BOOL:
                    return setting->bool_cb(value->bool_val, setting->cb_arg);

                case GOLIOTH_SETTING_VALUE_TYPE_FLOAT:
                    return setting->float_cb(value->float_val, setting->cb_arg);

                case GOLIOTH_SETTING_VALUE_TYPE_STRING:
                    return setting->string_cb(value->str_val.data,
                                              value->str_val.len,
                                              setting->cb_arg);

                default:
                    LOG_ERR("Unknown settings type");
                    break;
            }
        }
    }

    return -ENOENT;
}
