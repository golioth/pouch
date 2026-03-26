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

/*--------------------------------------------------
 * Logging
 *------------------------------------------------*/

#include <esp_log.h>
#include <esp_log_buffer.h>

#define POUCH_LOG_LEVEL_NONE_INTERNAL ESP_LOG_NONE
#define POUCH_LOG_LEVEL_ERR_INTERNAL ESP_LOG_ERROR
#define POUCH_LOG_LEVEL_WRN_INTERNAL ESP_LOG_WARN
#define POUCH_LOG_LEVEL_INF_INTERNAL ESP_LOG_INFO
#define POUCH_LOG_LEVEL_DBG_INTERNAL ESP_LOG_DEBUG
#define POUCH_LOG_LEVEL_VERBOSE_INTERNAL ESP_LOG_VERBOSE

#define POUCH_LOG_REGISTER_INTERNAL(tag, level) \
    static const char *__attribute((unused)) POUCH_LOG_TAG = #tag

#define POUCH_LOG_ERR_INTERNAL(tag, ...) ESP_LOGE(tag, __VA_ARGS__)
#define POUCH_LOG_WRN_INTERNAL(tag, ...) ESP_LOGW(tag, __VA_ARGS__)
#define POUCH_LOG_INF_INTERNAL(tag, ...) ESP_LOGI(tag, __VA_ARGS__)
#define POUCH_LOG_DBG_INTERNAL(tag, ...) ESP_LOGD(tag, __VA_ARGS__)
#define POUCH_LOG_VERBOSE_INTERNAL(tag, ...) ESP_LOGV(tag, __VA_ARGS__)

#define POUCH_LOG_HEXDUMP_INTERNAL(tag, buf, size, label) \
    ESP_LOGD(tag, "hexdump: %s", label);                  \
    ESP_LOG_BUFFER_HEX_LEVEL(tag, buf, size, ESP_LOG_DEBUG)

#define POUCH_LOG_FLUSH_INTERNAL()
