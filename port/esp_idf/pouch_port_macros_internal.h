/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "../freertos/freertos_port_layer.h"

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
 * Iterable Sections
 *------------------------------------------------*/

/* Helper macros for this port */
#define ESPIDF_ITERABLE_START(secname) _##secname##_start
#define ESPIDF_ITERABLE_END(secname) _##secname##_end

#define POUCH_TYPE_SECTION_START_INTERNAL(secname) ESPIDF_ITERABLE_START(secname)

#define POUCH_STRUCT_SECTION_COUNT_INTERNAL(struct_type, dst)           \
    do                                                                  \
    {                                                                   \
        extern struct struct_type ESPIDF_ITERABLE_START(struct_type)[]; \
        extern struct struct_type ESPIDF_ITERABLE_END(struct_type)[];   \
        *(dst) = ((uintptr_t) ESPIDF_ITERABLE_END(struct_type)          \
                  - (uintptr_t) ESPIDF_ITERABLE_START(struct_type))     \
            / sizeof(struct struct_type);                               \
    } while (0)

#define POUCH_TYPE_SECTION_FOREACH_INTERNAL(type, secname, iterator)                               \
    extern type ESPIDF_ITERABLE_START(secname)[];                                                  \
    extern type ESPIDF_ITERABLE_END(secname)[];                                                    \
    for (type *iterator = ESPIDF_ITERABLE_START(secname); iterator < ESPIDF_ITERABLE_END(secname); \
         iterator++)


#define POUCH_STRUCT_SECTION_GET_INTERNAL(struct_type, i, dst)          \
    do                                                                  \
    {                                                                   \
        extern struct struct_type ESPIDF_ITERABLE_START(struct_type)[]; \
        *(dst) = &ESPIDF_ITERABLE_START(struct_type)[i];                \
    } while (0)

/**
 * @brief Defines a new element for an iterable section for a generic type.
 *
 * A matching section must be added to the linker script. For ESP-IDF, use a linker fragment (.lf)
 * file. The sections/entrires/scheme/mapping values in the linker script must match the variables
 * passed by this macro.
 *
 * Register your linker fragment by adding a directive to the idf_component_register() call in
 * CMakeLists.txt:
 *   LDFRAGMENTS my_linker_fragment.lf
 *
 * This topic is detailed in the ESP-IDF documentation:
 * https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/linker-script-generation.html#creating-and-specifying-a-linker-fragment-file
 *
 * And example linker fragment is available in the ESP-IDF tree:
 * https://github.com/espressif/esp-idf/blob/master/examples/build_system/cmake/plugins/components/plugins/linker.lf
 *
 * The concept of Iterable Sections is based on the Zephyr RTOS definition:
 * https://docs.zephyrproject.org/latest/kernel/iterable_sections/index.html
 */
#define POUCH_TYPE_SECTION_ITERABLE_INTERNAL(type, varname, secname, section_postfix) \
    __attribute__((used)) __attribute__((aligned(__alignof__(type))))                 \
    __attribute__((section("._" #secname ".static." #section_postfix "_"))) type varname

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

/*--------------------------------------------------
 * Miscellaneous
 *------------------------------------------------*/

#include <esp_assert.h>

#define POUCH_STATIC_ASSERT_INTERNAL(EXPR, ...) ESP_STATIC_ASSERT(EXPR, __VA_ARGS__)

/*--------------------------------------------------
 * Mutex
 *------------------------------------------------*/

/** This section is implemented in port/freertos/freertos_port_layer.h */
