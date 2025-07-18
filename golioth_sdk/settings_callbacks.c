/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(setttings_callbacks, LOG_LEVEL_WRN);

#include <errno.h>
#include <stddef.h>

#include <golioth/settings_callbacks.h>

#include "settings.h"

struct registered_setting
{
    const char *key;
    enum setting_value_type type;
    union
    {
        golioth_int_setting_cb int_cb;
        golioth_bool_setting_cb bool_cb;
        golioth_float_setting_cb float_cb;
        golioth_string_setting_cb string_cb;
    };
    void *cb_arg;
} registered_settings[CONFIG_GOLIOTH_SETTINGS_MAX_NUM_CALLBACKS] = {0};

static struct registered_setting *alloc_setting(void)
{
    for (struct registered_setting *setting = registered_settings;
         setting < registered_settings + CONFIG_GOLIOTH_SETTINGS_MAX_NUM_CALLBACKS;
         setting++)
    {
        if (NULL == setting->key)
        {
            return setting;
        }
    }

    LOG_ERR("Exceded CONFIG_GOLIOTH_SETTINGS_MAX_NUM_CALLBACKS (%d)",
            CONFIG_GOLIOTH_SETTINGS_MAX_NUM_CALLBACKS);

    return NULL;
}

int golioth_settings_receive_one(const struct setting_value *value)
{
    for (struct registered_setting *setting = registered_settings;
         setting < registered_settings + CONFIG_GOLIOTH_SETTINGS_MAX_NUM_CALLBACKS;
         setting++)
    {
        if (NULL == setting->key)
        {
            return -ENOENT;
        }

        if (0 == strcmp(setting->key, value->key))
        {
            if (setting->type != value->type)
            {
                return -EFTYPE;
            }

            switch (setting->type)
            {
                case SETTING_VALUE_TYPE_INT:
                    return setting->int_cb(value->int_val, setting->cb_arg);

                case SETTING_VALUE_TYPE_BOOL:
                    return setting->bool_cb(value->bool_val, setting->cb_arg);

                case SETTING_VALUE_TYPE_FLOAT:
                    return setting->float_cb(value->float_val, setting->cb_arg);

                case SETTING_VALUE_TYPE_STRING:
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

int golioth_settings_register_int(const char *setting_name,
                                  golioth_int_setting_cb callback,
                                  void *callback_arg)
{
    if (!callback)
    {
        LOG_ERR("Callback must not be NULL");
        return -EINVAL;
    }

    struct registered_setting *new_setting = alloc_setting();
    if (!new_setting)
    {
        return -ENOMEM;
    }

    new_setting->key = setting_name;
    new_setting->type = SETTING_VALUE_TYPE_INT;
    new_setting->int_cb = callback;
    new_setting->cb_arg = callback_arg;

    return 0;
}

int golioth_settings_register_bool(const char *setting_name,
                                   golioth_bool_setting_cb callback,
                                   void *callback_arg)
{
    if (!callback)
    {
        LOG_ERR("Callback must not be NULL");
        return -EINVAL;
    }

    struct registered_setting *new_setting = alloc_setting();
    if (!new_setting)
    {
        return -ENOMEM;
    }

    new_setting->key = setting_name;
    new_setting->type = SETTING_VALUE_TYPE_BOOL;
    new_setting->bool_cb = callback;
    new_setting->cb_arg = callback_arg;

    return 0;
}

int golioth_settings_register_float(const char *setting_name,
                                    golioth_float_setting_cb callback,
                                    void *callback_arg)
{
    if (!callback)
    {
        LOG_ERR("Callback must not be NULL");
        return -EINVAL;
    }

    struct registered_setting *new_setting = alloc_setting();
    if (!new_setting)
    {
        return -ENOMEM;
    }

    new_setting->key = setting_name;
    new_setting->type = SETTING_VALUE_TYPE_FLOAT;
    new_setting->float_cb = callback;
    new_setting->cb_arg = callback_arg;

    return 0;
}

int golioth_settings_register_string(const char *setting_name,
                                     golioth_string_setting_cb callback,
                                     void *callback_arg)
{
    if (!callback)
    {
        LOG_ERR("Callback must not be NULL");
        return -EINVAL;
    }

    struct registered_setting *new_setting = alloc_setting();
    if (!new_setting)
    {
        return -ENOMEM;
    }

    new_setting->key = setting_name;
    new_setting->type = SETTING_VALUE_TYPE_STRING;
    new_setting->string_cb = callback;
    new_setting->cb_arg = callback_arg;

    return 0;
}