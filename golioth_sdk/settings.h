/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <golioth/settings_callbacks.h>

struct setting_value
{
    const char *key;
    enum golioth_setting_value_type type;
    union
    {
        bool bool_val;
        int32_t int_val;
        double float_val;
        struct
        {
            const char *data;
            size_t len;
        } str_val;
    };
};

int golioth_settings_receive_one(const struct setting_value *value);
