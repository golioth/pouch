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

#define POUCH_STATIC_ASSERT(EXPR, ...) POUCH_STATIC_ASSERT_INTERNAL(EXPR, __VA_ARGS__)

#ifndef STRINGIFY
#define STRINGIFY(s) #s
#endif

#ifndef DIV_ROUND_UP
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
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
 * Atomic
 *------------------------------------------------*/

#include <limits.h>

typedef intptr_t pouch_atomic_t;

/**
 * @brief Define number of bits in pouch_atomic_t
 */
#define POUCH_ATOMIC_BITS (sizeof(pouch_atomic_t) * CHAR_BIT)

/**
 * @brief Initialize a pouch_atomic_t
 *
 * @param i Value to assign to atomic variable.
 */
#define POUCH_ATOMIC_INIT(i) (i)

/**
 * @brief Define an array of pouch_atomic_t variables
 *
 * Defines an array of pouch_atomic_t that contain a combined number of bits at least /p num_bits
 * in size. When used at file scope the array will be initialized to 0, otherwise uninitialized.
 *
 * @param name Name of array to define.
 * @param num_bits Minimum nuber of bits contained by all array members combined.
 */
#define POUCH_ATOMIC_DEFINE(name, num_bits) \
    pouch_atomic_t name[DIV_ROUND_UP(num_bits, POUCH_ATOMIC_BITS)]

/**
 * @brief Atomic decrement by 1.
 *
 * @param target Address of atomic variable
 * @return Value of target before decrement
 */
pouch_atomic_t pouch_atomic_dec(pouch_atomic_t *target);

/**
 * @brief Atomic increment by 1.
 *
 * @param target Address of atomic variable
 * @return Value of target before increment
 */
pouch_atomic_t pouch_atomic_inc(pouch_atomic_t *target);

/**
 * @brief Atomic get.
 *
 * @param target Address of atomic variable
 * @return Value of target
 */
pouch_atomic_t pouch_atomic_get(const pouch_atomic_t *target);

/**
 * @brief Atomic clear.
 *
 * @param target Address of atomic variable to set to 0
 * @return Value of target before clear.
 */
pouch_atomic_t pouch_atomic_clear(pouch_atomic_t *target);

/**
 * @brief Atomic set.
 *
 * @param target Address of atomic variable
 * @param value Value to set
 * @return Value of target before set.
 */
pouch_atomic_t pouch_atomic_set(pouch_atomic_t *target, pouch_atomic_t value);

/**
 * @brief Atomically clear a bit
 *
 * A pointer to an array of pouch_atomic_t may be supplied as the /p target when /p bit is larger
 * than POUCH_ATOMIC_BITS. See POUCH_ATOMIC_DEFINE().
 *
 * @param target Address of atomic variable
 * @param bit Bit number to be cleared
 */
void pouch_atomic_clear_bit(pouch_atomic_t *target, int bit);

/**
 * @brief Atomically set a bit
 *
 * A pointer to an array of pouch_atomic_t may be supplied as the /p target when /p bit is larger
 * than POUCH_ATOMIC_BITS. See POUCH_ATOMIC_DEFINE().
 *
 * @param target Address of atomic variable
 * @param bit Bit number to be set
 */
void pouch_atomic_set_bit(pouch_atomic_t *target, int bit);

/**
 * @brief Atomically test the state of a bit
 *
 * A pointer to an array of pouch_atomic_t may be supplied as the /p target when /p bit is larger
 * than POUCH_ATOMIC_BITS. See POUCH_ATOMIC_DEFINE().
 *
 * @param target Address of atomic variable
 * @param bit Bit number to test
 * @return true if bit is set, false if bit is clear
 */
bool pouch_atomic_test_bit(const pouch_atomic_t *target, int bit);

/**
 * @brief Atomically clear bit and return its previous state
 *
 * A pointer to an array of pouch_atomic_t may be supplied as the /p target when /p bit is larger
 * than POUCH_ATOMIC_BITS. See POUCH_ATOMIC_DEFINE().
 *
 * @param target Address of atomic variable
 * @param bit Bit number to test
 * @return false if bit was already clear, true if it wasn't
 */
bool pouch_atomic_test_and_clear_bit(pouch_atomic_t *target, int bit);

/**
 * @brief Atomically set bit and return its previous state
 *
 * A pointer to an array of pouch_atomic_t may be supplied as the /p target when /p bit is larger
 * than POUCH_ATOMIC_BITS. See POUCH_ATOMIC_DEFINE().
 *
 * @param target Address of atomic variable
 * @param bit Bit number to test
 * @return true if bit was already set, false if it wasn't
 */
bool pouch_atomic_test_and_set_bit(pouch_atomic_t *target, int bit);

/*--------------------------------------------------
 * Big Endian
 *------------------------------------------------*/

/**
 *  @brief Get a 16-bit integer stored in big-endian format.
 *
 *  @param src Big-endian 16-bit integer.
 *  @return 16-bit integer in host endianness.
 */
uint16_t pouch_get_be16(const uint8_t src[2]);

/**
 *  @brief Get a 32-bit integer stored in big-endian format.
 *
 *  @param src Big-endian 32-bit integer.
 *  @return 32-bit integer in host endianness.
 */
uint32_t pouch_get_be32(const uint8_t src[4]);

/**
 *  @brief Get a 64-bit integer stored in big-endian format.
 *
 *  @param src Big-endian 64-bit integer.
 *  @return 64-bit integer in host endianness.
 */
uint64_t pouch_get_be64(const uint8_t src[8]);

/**
 *  @brief Put a 16-bit integer as big-endian to arbitrary location.
 *
 *  @param val 16-bit integer in host endianness.
 *  @param dst Address to store the result.
 */
void pouch_put_be16(uint16_t val, uint8_t dst[2]);

/*--------------------------------------------------
 * Iterable Sections
 *------------------------------------------------*/

/**
 * @brief Iterable section start symbol for a struct type
 *
 * @param[in] struct_type data type of section
 */
#define POUCH_STRUCT_SECTION_START(struct_type) POUCH_TYPE_SECTION_START_INTERNAL(struct_type)

/**
 * @brief Count elements in a section.
 *
 * @param[in]  struct_type Struct type
 * @param[out] dst Pointer to location where result is written.
 */
#define POUCH_STRUCT_SECTION_COUNT(struct_type, dst) \
    POUCH_STRUCT_SECTION_COUNT_INTERNAL(struct_type, dst)

/**
 * @brief Iterate over a specified iterable section for a generic type
 *
 * @param[in]  type Type of element
 * @param[in]  secname Name of output section
 * @param[out]  iterator Struct pointer provided by macro, incremented for each iteration.
 */
#define POUCH_TYPE_SECTION_FOREACH(type, secname, iterator) \
    POUCH_TYPE_SECTION_FOREACH_INTERNAL(type, secname, iterator)

/**
 * @brief Get element from section.
 *
 * @param[in]  struct_type Struct type.
 * @param[in]  i Index.
 * @param[out] dst Pointer to location where pointer to element is written.
 */
#define POUCH_STRUCT_SECTION_GET(struct_type, i, dst) \
    POUCH_STRUCT_SECTION_GET_INTERNAL(struct_type, i, dst)

/**
 * @brief Defines a new element for an iterable section for a generic type.
 *
 * @note This function requires a matching directive in the linker script. Please see the definition
 * of POUCH_TYPE_SECTION_ITERABLE_INTERNAL in the port you are using for more information.
 *
 * @param[in]  type Data type of variable
 * @param[in]  varname Name of variable to place in section
 * @param[in]  secname Type name of iterable section.
 * @param[in]  section_postfix Postfix to use in section name
 */
#define POUCH_TYPE_SECTION_ITERABLE(type, varname, secname, section_postfix) \
    POUCH_TYPE_SECTION_ITERABLE_INTERNAL(type, varname, secname, section_postfix)

/**
 * @brief iterable section start symbol for a struct type
 *
 * @param[in]  struct_type Data type of section
 *
 * @note: this is a wrapper macro that doesn't need porting
 */
#define POUCH_STRUCT_SECTION_START_EXTERN(struct_type) \
    extern struct struct_type POUCH_STRUCT_SECTION_START(struct_type)[]

/**
 * @brief Iterate over a specified iterable section.
 *
 * @param[in]   struct_type Data type of section
 * @param[out]  iterator Struct pointer provided by macro, incremented for each iteration.
 *
 * @note: this is a wrapper macro that doesn't need porting
 */
#define POUCH_STRUCT_SECTION_FOREACH(struct_type, iterator) \
    POUCH_TYPE_SECTION_FOREACH(struct struct_type, struct_type, iterator)

/**
 * @brief Defines a new element for an iterable section.
 *
 * @param[in]  struct_type Data type of section
 * @param[in]  varname Name of variable to place in section
 *
 * @note: this is a wrapper macro that doesn't need porting
 */
#define POUCH_STRUCT_SECTION_ITERABLE(struct_type, varname) \
    POUCH_TYPE_SECTION_ITERABLE(struct struct_type, varname, struct_type, varname)

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
