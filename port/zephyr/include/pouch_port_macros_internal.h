/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <zephyr/kernel.h>

/*--------------------------------------------------
 * Atomic
 *------------------------------------------------*/

#include <zephyr/sys/atomic_types.h>

typedef atomic_t pouch_atomic_internal_t;

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
 * Iterable Sections
 *------------------------------------------------*/

#include <zephyr/sys/iterable_sections.h>

#define POUCH_TYPE_SECTION_START_INTERNAL(secname) TYPE_SECTION_START(secname)

#define POUCH_STRUCT_SECTION_COUNT_INTERNAL(struct_type, dst) STRUCT_SECTION_COUNT(struct_type, dst)

#define POUCH_TYPE_SECTION_FOREACH_INTERNAL(type, secname, iterator) \
    TYPE_SECTION_FOREACH(type, secname, iterator)

#define POUCH_STRUCT_SECTION_GET_INTERNAL(struct_type, i, dst) \
    STRUCT_SECTION_GET(struct_type, i, dst)

/**
 * @brief Defines a new element for an iterable section for a generic type.
 *
 * A matching section must be added to the linker script. For Zephyr, use an .ld file that calls
 * ITERABLE_SECTION_ROM() or ITERABLE_SECTION_RAM() to define the section using the same values as
 * are passed to this macro.
 *
 * Register your linker file by adding a directive to CMakeLists.txt:
 *   zephyr_linker_sources(SECTIONS my_linker_file.ld)
 *
 * Learn more about Iterable Sections in the Zephyr documentation:
 * https://docs.zephyrproject.org/latest/kernel/iterable_sections/index.html
 */
#define POUCH_TYPE_SECTION_ITERABLE_INTERNAL(type, varname, secname, section_postfix) \
    TYPE_SECTION_ITERABLE(type, varname, secname, section_postfix)

/*--------------------------------------------------
 * Linked List
 *------------------------------------------------*/

typedef sys_slist_t pouch_slist_internal_t;

typedef sys_snode_t pouch_slist_node_internal_t;

/*--------------------------------------------------
 * Logging
 *------------------------------------------------*/

#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>

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

/*--------------------------------------------------
 * Message Queue
 *------------------------------------------------*/

typedef struct k_msgq pouch_msgq_internal_t;

/*--------------------------------------------------
 * Miscellaneous
 *------------------------------------------------*/

#include <zephyr/toolchain/gcc.h>

#define POUCH_STATIC_ASSERT_INTERNAL(EXPR, ...) BUILD_ASSERT(EXPR, __VA_ARGS__)

/*--------------------------------------------------
 * Mutex
 *------------------------------------------------*/

typedef struct k_mutex pouch_mutex_internal_t;

#define POUCH_MUTEX_DEFINE_INTERNAL(name) K_MUTEX_DEFINE(name)

/*--------------------------------------------------
 * Time
 *------------------------------------------------*/

#define POUCH_FOREVER_INTERNAL K_FOREVER
#define POUCH_NO_WAIT_INTERNAL K_NO_WAIT
#define POUCH_MSEC_INTERNAL(ms) K_MSEC(ms)

typedef k_timeout_t pouch_timeout_internal_t;
typedef k_timepoint_t pouch_timepoint_internal_t;

/*--------------------------------------------------
 * Work Queue
 *------------------------------------------------*/

typedef struct k_work_q pouch_work_q_internal_t;

typedef struct k_work pouch_work_internal_t;

#define POUCH_THREAD_STACK_DEFINE_INTERNAL(name, size) K_THREAD_STACK_DEFINE(name, size)
