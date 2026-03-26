/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "pouch_port_macros_internal.h"

/*--------------------------------------------------
 * Misc
 *------------------------------------------------*/

#ifndef STRINGIFY
#define STRINGIFY(s) #s
#endif

/*--------------------------------------------------
 * Application Startup Hook
 *------------------------------------------------*/

/** Register function to run at application startup
 *
 * Example:
 *
 * static void my_startup_function(void)
 * {
 *     initialize_something();
 * }
 * POUCH_APPLICATION_STARTUP_HOOK(my_startup_function);
 *
 */
#define POUCH_APPLICATION_STARTUP_HOOK(_function) POUCH_APPLICATION_STARTUP_HOOK_INTERNAL(_function)

/*--------------------------------------------------
 * Logging
 *------------------------------------------------*/

/** Log Level Defines */
#define POUCH_LOG_LEVEL_NONE POUCH_LOG_LEVEL_NONE_INTERNAL
#define POUCH_LOG_LEVEL_ERR POUCH_LOG_LEVEL_ERR_INTERNAL
#define POUCH_LOG_LEVEL_WRN POUCH_LOG_LEVEL_WRN_INTERNAL
#define POUCH_LOG_LEVEL_INF POUCH_LOG_LEVEL_INF_INTERNAL
#define POUCH_LOG_LEVEL_DBG POUCH_LOG_LEVEL_DBG_INTERNAL
#define POUCH_LOG_LEVEL_VERBOSE POUCH_LOG_LEVEL_VERBOSE_INTERNAL

/** Register the file with the logging system
 *
 * Example usage:
 * - POUCH_LOG_REGISTER(my_module, POUCH_LOG_LEVEL_DBG);
 *
 * @param tag The name to use for this file's logging module (no quotes).
 * @param level Log level to use for this file.
 *
 * @note The log level may be limited by other RTOS settings.
 * @note The log level is not honored by ESP-IDF.
 */
#define POUCH_LOG_REGISTER(tag, level) POUCH_LOG_REGISTER_INTERNAL(tag, level)

/** Logging macros
 *
 * POUCH_LOG_REGISTER(<name>, <log level>) must be called before using these macros.
 *
 * Example Usage:
 * - POUCH_LOG_INF("This is a logging message!");
 * - POUCH_LOG_ERR("Failed to execute: %d", err);
 */
#define POUCH_LOG_NONE(...)
#define POUCH_LOG_ERR(...) POUCH_LOG_ERR_INTERNAL(POUCH_LOG_TAG, __VA_ARGS__)
#define POUCH_LOG_WRN(...) POUCH_LOG_WRN_INTERNAL(POUCH_LOG_TAG, __VA_ARGS__)
#define POUCH_LOG_INF(...) POUCH_LOG_INF_INTERNAL(POUCH_LOG_TAG, __VA_ARGS__)
#define POUCH_LOG_DBG(...) POUCH_LOG_DBG_INTERNAL(POUCH_LOG_TAG, __VA_ARGS__)
#define POUCH_LOG_VERBOSE(...) POUCH_LOG_VERBOSE_INTERNAL(POUCH_LOG_TAG, __VA_ARGS__)

/** Log a hexdump of a memory area
 *
 * The hexdump will be logged at the POUCH_LOG_LEVEL_DBG level. POUCH_LOG_REGISTER(<name>, <log
 * level>) must be called before using this macro.
 *
 * Example Usage:
 * - POUCH_LOG_HEXDUMP(buffer, buffer_size, "Buffer contents");
 *
 * @param buf Buffer where data is located
 * @param size Number of bytes to show in the hexdump
 * @label String to use in the logs to identify the hexdump
 */
#define POUCH_LOG_HEXDUMP(buf, size, label) \
    POUCH_LOG_HEXDUMP_INTERNAL(POUCH_LOG_TAG, buf, size, label)

/** Flush any pending logs */
#define POUCH_LOG_FLUSH() POUCH_LOG_FLUSH_INTERNAL()
