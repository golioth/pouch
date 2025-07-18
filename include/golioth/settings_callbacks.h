/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * Callback function types for golioth_settings_register_*
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

/**
 * Register a specific setting of type int
 *
 * @param setting_name The name of the setting. This is expected to be a literal
 *     string, therefore on the pointer is registered (not a full copy of the string).
 * @param callback Callback function that will be called when the setting value is
 *     received from Golioth cloud
 * @param callback_arg General-purpose user argument, forwarded as-is to
 *     callback, can be NULL.
 *
 * @return 0 on success or a negative error code on failure.
 */
int golioth_settings_register_int(const char *setting_name,
                                  golioth_int_setting_cb callback,
                                  void *callback_arg);

/**
 * Same as @ref golioth_settings_register_int, but for type bool.
 */
int golioth_settings_register_bool(const char *setting_name,
                                   golioth_bool_setting_cb callback,
                                   void *callback_arg);

/**
 * Same as @ref golioth_settings_register_int, but for type bool.
 */
int golioth_settings_register_float(const char *setting_name,
                                    golioth_float_setting_cb callback,
                                    void *callback_arg);

/**
 * Same as @ref golioth_settings_register_int, but for type bool.
 */
int golioth_settings_register_string(const char *setting_name,
                                     golioth_string_setting_cb callback,
                                     void *callback_arg);
