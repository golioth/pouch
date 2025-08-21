/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <golioth/settings_types.h>

/**
 * Callback function types for GOLIOTH_SETTINGS_HANDLER() *
 *
 * @param new_value The setting value received from Golioth cloud
 * @param arg User's registered callback arg
 *
 * @return 0 on success or a negative error code on failure.
 */
typedef int (*golioth_int_setting_cb)(int32_t new_value, void *arg);
typedef int (*golioth_bool_setting_cb)(bool new_value, void *arg);
typedef int (*golioth_float_setting_cb)(double new_value, void *arg);
typedef int (*golioth_string_setting_cb)(const char *new_value, size_t len, void *arg);

struct golioth_settings_handler
{
    const char *key;
    enum golioth_setting_value_type type;
    union
    {
        golioth_int_setting_cb int_cb;
        golioth_bool_setting_cb bool_cb;
        golioth_float_setting_cb float_cb;
        golioth_string_setting_cb string_cb;
        void *generic;
    };
    void *cb_arg;
};

/**
 * Register a settings handler.
 *
 * Use this macro to register a callback to be executed each time a setting
 * is updated. The data type of the setting is inferred from the callback
 * signature.
 *
 * NOTE: This callback may be called even if the value of the setting has
 * not changed.
 *
 * @param _name     The name of the setting. This must match the name on
 *                  Golioth.
 * @param _function The callback function to execute.
 * @param _arg      A user supplied argument that will be passed to the
 *                  callback.
 */
#define GOLIOTH_SETTINGS_HANDLER(_name, _function, _arg)                   \
    static const STRUCT_SECTION_ITERABLE(golioth_settings_handler,         \
                                         CONCAT(handler_, _function)) = {  \
        .key = #_name,                                                     \
        .type = _Generic((_function),                                      \
            golioth_int_setting_cb: GOLIOTH_SETTING_VALUE_TYPE_INT,        \
            golioth_bool_setting_cb: GOLIOTH_SETTING_VALUE_TYPE_BOOL,      \
            golioth_float_setting_cb: GOLIOTH_SETTING_VALUE_TYPE_FLOAT,    \
            golioth_string_setting_cb: GOLIOTH_SETTING_VALUE_TYPE_STRING), \
        .generic = _function,                                              \
        .cb_arg = _arg,                                                    \
    }
