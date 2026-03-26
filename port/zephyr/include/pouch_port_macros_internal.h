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

/*--------------------------------------------------
 * Logging
 *------------------------------------------------*/

#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>

#define POUCH_LOG_LEVEL_NONE_INTERNAL LOG_LEVEL_NONE
#define POUCH_LOG_LEVEL_ERR_INTERNAL LOG_LEVEL_ERR
#define POUCH_LOG_LEVEL_WRN_INTERNAL LOG_LEVEL_WRN
#define POUCH_LOG_LEVEL_INF_INTERNAL LOG_LEVEL_INF
#define POUCH_LOG_LEVEL_DBG_INTERNAL LOG_LEVEL_DBG
#define POUCH_LOG_LEVEL_VERBOSE_INTERNAL LOG_LEVEL_DBG

/* The Zephyr port doesn't use POUCH_LOG_TAG but it needs to be defined for macros to work */
#define POUCH_LOG_TAG

#define POUCH_LOG_REGISTER_INTERNAL(tag, level) LOG_MODULE_REGISTER(tag, level)

#define POUCH_LOG_ERR_INTERNAL(tag, ...) LOG_ERR(__VA_ARGS__)
#define POUCH_LOG_WRN_INTERNAL(tag, ...) LOG_WRN(__VA_ARGS__)
#define POUCH_LOG_INF_INTERNAL(tag, ...) LOG_INF(__VA_ARGS__)
#define POUCH_LOG_DBG_INTERNAL(tag, ...) LOG_DBG(__VA_ARGS__)
#define POUCH_LOG_VERBOSE_INTERNAL(tag, ...) LOG_DBG(__VA_ARGS__)

#define POUCH_LOG_HEXDUMP_INTERNAL(tag, buf, size, label) LOG_HEXDUMP_DBG(buf, size, label)

#define POUCH_LOG_FLUSH_INTERNAL() LOG_PANIC()
