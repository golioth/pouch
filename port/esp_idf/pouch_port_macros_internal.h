/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

/*--------------------------------------------------
 * Application Startup Hook
 *------------------------------------------------*/

#include <esp_private/startup_internal.h>

#define POUCH_APPLICATION_STARTUP_HOOK_INTERNAL(_function)                   \
    ESP_SYSTEM_INIT_FN(_function##_app_startup_hook, SECONDARY, BIT(0), 300) \
    {                                                                        \
        _function();                                                         \
        return ESP_OK;                                                       \
    }
