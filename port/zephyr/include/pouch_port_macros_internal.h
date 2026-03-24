/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

/*--------------------------------------------------
 * Application Startup Hook
 *------------------------------------------------*/

#include <zephyr/init.h>

#define POUCH_APPLICATION_STARTUP_HOOK_INTERNAL(_function) \
    static int _function##_app_startup_hook(void)          \
    {                                                      \
        _function();                                       \
        return 0;                                          \
    }                                                      \
    SYS_INIT(_function##_app_startup_hook, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY)
