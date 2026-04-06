/*
 * Copyright (c) 2026 Golioth, Inc.
 * Copyright (c) 2011-2014, Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "pouch_port_macros_internal.h"
#include <stdint.h>

/*--------------------------------------------------
 * Miscellaneous
 *------------------------------------------------*/

#define POUCH_STATIC_ASSERT(EXPR, ...) POUCH_STATIC_ASSERT_INTERNAL(EXPR, __VA_ARGS__)

#ifndef STRINGIFY
#define STRINGIFY(s) #s
#endif

#ifndef DIV_ROUND_UP
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#endif

#ifndef CONTAINER_OF

/*
 * When not present, use the Zephyr definition of CONTAINER_OF from:
 * https://github.com/zephyrproject-rtos/zephyr/blob/v4.3.0/include/zephyr/sys/util.h
 */

#ifndef __cplusplus

#define SAME_TYPE(a, b) __builtin_types_compatible_p(__typeof__(a), __typeof__(b))

#define CONTAINER_OF_VALIDATE(ptr, type, field)                                            \
    POUCH_STATIC_ASSERT(SAME_TYPE(*(ptr), ((type *) 0)->field) || SAME_TYPE(*(ptr), void), \
                        "pointer type mismatch in CONTAINER_OF");

#else
#define CONTAINER_OF_VALIDATE(ptr, type, field)
#endif

/**
 * @brief Get a pointer to a structure containing the element
 *
 * Example:
 *
 *	struct foo {
 *		int bar;
 *	};
 *
 *	struct foo my_foo;
 *	int *ptr = &my_foo.bar;
 *
 *	struct foo *container = CONTAINER_OF(ptr, struct foo, bar);
 *
 * Above, @p container points at @p my_foo.
 *
 * @param ptr pointer to a structure element
 * @param type name of the type that @p ptr is an element of
 * @param field the name of the field within the struct @p ptr points to
 * @return a pointer to the structure that contains @p ptr
 */
#define CONTAINER_OF(ptr, type, field)                         \
    ({                                                         \
        CONTAINER_OF_VALIDATE(ptr, type, field)                \
        ((type *) (((char *) (ptr)) - offsetof(type, field))); \
    })

#endif  // CONTAINER_OF

/**
 * @brief Allow RTOS scheduler to run other threads
 */
void pouch_yield(void);

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

typedef pouch_atomic_internal_t pouch_atomic_t;

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
long pouch_atomic_dec(pouch_atomic_t *target);

/**
 * @brief Atomic increment by 1.
 *
 * @param target Address of atomic variable
 * @return Value of target before increment
 */
long pouch_atomic_inc(pouch_atomic_t *target);

/**
 * @brief Atomic get.
 *
 * @param target Address of atomic variable
 * @return Value of target
 */
long pouch_atomic_get_value(const pouch_atomic_t *target);

/**
 * @brief Atomic clear.
 *
 * @param target Address of atomic variable to set to 0
 * @return Value of target before clear.
 */
long pouch_atomic_clear(pouch_atomic_t *target);

/**
 * @brief Atomic set.
 *
 * @param target Address of atomic variable
 * @param value Value to set
 * @return Value of target before set.
 */
long pouch_atomic_set(pouch_atomic_t *target, long value);

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
 * Linked List
 *------------------------------------------------*/

/**
 * @brief Type to use for singly linked lists
 *
 * @note Lists must be initialized before use.
 */
typedef pouch_slist_internal_t pouch_slist_t;

/**
 * @brief Type to use for singly linked lists nodes
 *
 * @note List nodes must be initialized before use.
 */
typedef pouch_slist_node_internal_t pouch_slist_node_t;

/**
 * @brief Initialize a singly linked list
 *
 * @param list List to be initialized
 */
void pouch_slist_init(pouch_slist_t *list);

/**
 * @brief Initialize a singly linked list node
 *
 * @param node Node to be initialized
 */
void pouch_slist_node_init(pouch_slist_node_t *node);

/**
 * @brief Append a node to the end of a singly linked list
 *
 * This function is not thread safe.
 *
 * @param list List onto which a node should be appended
 * @param node Node to append to list
 */
void pouch_slist_append(pouch_slist_t *list, pouch_slist_node_t *node);

/**
 * @brief Remove and return the head node of a singly linked list
 *
 * This function is not thread safe.
 *
 * @param list List from which to get a node
 *
 * @return Pointer to the node or NULL if the list was empty
 */
pouch_slist_node_t *pouch_slist_get(pouch_slist_t *list);

/**
 * @brief Peek the head node of a singly linked list
 *
 * The node will remain at the head of the list. This function is not thread safe.
 *
 * @param list List from which to peek a node
 *
 * @return Pointer to the head node or NULL if the list was empty
 */
pouch_slist_node_t *pouch_slist_peek_head(pouch_slist_t *list);

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

/*------------------------------------------------
 * Time
 *------------------------------------------------*/

/** Infinite timeout delay */
#define POUCH_FOREVER POUCH_FOREVER_INTERNAL
/** Timeout that expires immediately */
#define POUCH_NO_WAIT POUCH_NO_WAIT_INTERNAL
/** Timeout value in milliseconds */
#define POUCH_MSEC(ms) POUCH_MSEC_INTERNAL(ms)
/** Timeout value in seconds */
#define POUCH_SECONDS(s) (POUCH_MSEC_INTERNAL((1000 * (s))))

/** Timeout type */
typedef pouch_timeout_internal_t pouch_timeout_t;
/** Timepoint representing an absolute timestamp */
typedef pouch_timepoint_internal_t pouch_timepoint_t;

/** Get the corresponding timepoint for the given timeout */
pouch_timepoint_t pouch_timepoint_get(pouch_timeout_t timeout);
/** Get a timeout value from a timepoint */
pouch_timeout_t pouch_timepoint_timeout(pouch_timepoint_t tp);

/*--------------------------------------------------
 * Message Queue
 *------------------------------------------------*/

/** @brief Handle used for a message queue */
typedef pouch_msgq_internal_t pouch_msgq_t;

/**
 * @brief Initialize a message queue
 *
 * @param msgq Handle to use for this message queue.
 * @param msgq_buffer Pointer to memory supplied by caller to use as storage buffer.
 * @param msgq_buffer_size Size of the \p msgq_buffer.
 * @param msg_size Size of a single message.
 */
void pouch_msgq_init(pouch_msgq_t *msgq,
                     uint8_t *msgq_buffer,
                     size_t msgq_buffer_size,
                     size_t msg_size);

/**
 * @brief Put a message into the message queue
 *
 * Copies the messages and adds it to the queue.
 *
 * @param msgq Handle of message queue.
 * @param msgq_buffer[in] Pointer to message to store in the queue.
 * @param timeout Timeout in milliseconds.
 *
 * @return 0 on success, -EAGAIN when timeout reached before queueing the message.
 */
int pouch_msgq_put(pouch_msgq_t *msgq, const void *data, pouch_timeout_t timeout);

/**
 * @brief Get the next message from the message queue
 *
 * Fetch the oldest message and remove it from the queue.
 *
 * @param msgq Handle of message queue.
 * @param msgq_buffer[out] Buffer where the message will be stored
 * @param timeout Timeout in milliseconds.
 *
 * @return 0 on success or -ENOMSG when queue is empty.
 */
int pouch_msgq_get(pouch_msgq_t *msgq, void *buf, pouch_timeout_t timeout);

/*--------------------------------------------------
 * Mutex
 *------------------------------------------------*/

/** @brief Type to use for mutex operations */
typedef pouch_mutex_internal_t pouch_mutex_t;

/** @brief Define and statically initialize a mutex at compile-time
 *
 * @param name Name used for the mutex handle
 */
#define POUCH_MUTEX_DEFINE(name) POUCH_MUTEX_DEFINE_INTERNAL(name)

/** @brief Initialize a mutex
 *
 * @param mutex Pointer to a mutex
 */
void pouch_mutex_init(pouch_mutex_t *mutex);

/** @brief Lock a mutex
 *
 * @param mutex Pointer to a mutex
 * @param timeout Timeout in milliseconds
 *
 * @return true if mutex was locked, false if timeout reached without attaining lock
 */
bool pouch_mutex_lock(pouch_mutex_t *mutex, pouch_timeout_t timeout);

/** @brief Unlock a mutex
 *
 * @param mutex Pointer to a mutex
 *
 * @return true if mutex was unlocked, false if it was not unlocked
 */
bool pouch_mutex_unlock(pouch_mutex_t *mutex);
