/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <pouch/port.h>
#include <stdbool.h>
#include <stdint.h>
#include <zephyr/kernel.h>

/*--------------------------------------------------
 * Atomic
 *------------------------------------------------*/

#include <zephyr/sys/atomic.h>

/*
 * The Zephyr atomic implementation uses atomic_t that is a typedef of long so this compile-time
 * assert should always evaluate to true. This is here to catch any unexpected changes to the
 * bit-width in the future.
 */
POUCH_STATIC_ASSERT(sizeof(pouch_atomic_t) == sizeof(long),
                    "Pouch atomic port API requires sizeof(pouch_atomic_t) == sizeof(long)");

/* Zephyr atomic_t is a typedef for long so no need to cast in these wrappers */

long pouch_atomic_dec(pouch_atomic_t *target)
{
    return atomic_dec(target);
}

long pouch_atomic_inc(pouch_atomic_t *target)
{
    return atomic_inc(target);
}

long pouch_atomic_get_value(const pouch_atomic_t *target)
{
    return atomic_get(target);
}

long pouch_atomic_clear(pouch_atomic_t *target)
{
    return atomic_clear(target);
}

long pouch_atomic_set(pouch_atomic_t *target, long value)
{
    return atomic_set(target, (atomic_t) value);
}

void pouch_atomic_clear_bit(pouch_atomic_t *target, int bit)
{
    atomic_clear_bit(target, bit);
}

void pouch_atomic_set_bit(pouch_atomic_t *target, int bit)
{
    atomic_set_bit(target, bit);
}

bool pouch_atomic_test_bit(const pouch_atomic_t *target, int bit)
{
    return atomic_test_bit(target, bit);
}

bool pouch_atomic_test_and_clear_bit(pouch_atomic_t *target, int bit)
{
    return atomic_test_and_clear_bit(target, bit);
}

bool pouch_atomic_test_and_set_bit(pouch_atomic_t *target, int bit)
{
    return atomic_test_and_set_bit(target, bit);
}

/*--------------------------------------------------
 * Big Endian
 *------------------------------------------------*/

#include <zephyr/sys/byteorder.h>

uint16_t pouch_get_be16(const uint8_t src[2])
{
    return sys_get_be16(src);
}

uint32_t pouch_get_be32(const uint8_t src[4])
{
    return sys_get_be32(src);
}

uint64_t pouch_get_be64(const uint8_t src[8])
{
    return sys_get_be64(src);
}

void pouch_put_be16(uint16_t val, uint8_t dst[2])
{
    return sys_put_be16(val, dst);
}

/*--------------------------------------------------
 * Linked List
 *------------------------------------------------*/

void pouch_slist_init(pouch_slist_t *list)
{
    sys_slist_init(list);
}

void pouch_slist_node_init(pouch_slist_node_t *node)
{
    /* Zephyr slist nodes don't need to be initialized */
    return;
}

void pouch_slist_append(pouch_slist_t *list, pouch_slist_node_t *node)
{
    sys_slist_append(list, node);
}

pouch_slist_node_t *pouch_slist_get(pouch_slist_t *list)
{
    return sys_slist_get(list);
}

pouch_slist_node_t *pouch_slist_peek_head(pouch_slist_t *list)
{
    return sys_slist_peek_head(list);
}

/*--------------------------------------------------
 * Message Queue
 *------------------------------------------------*/

void pouch_msgq_init(pouch_msgq_t *msgq,
                     uint8_t *msgq_buffer,
                     size_t msgq_buffer_size,
                     size_t msg_size)
{
    k_msgq_init(msgq, msgq_buffer, msg_size, msgq_buffer_size / msg_size);
}

int pouch_msgq_put(pouch_msgq_t *msgq, const void *data, int32_t timeout_ms)
{
    int result = k_msgq_put(msgq,
                            data,
                            (0 < timeout_ms)        ? K_MSEC(timeout_ms)
                                : (timeout_ms == 0) ? K_NO_WAIT
                                                    : K_FOREVER);

    return (0 == result) ? 0 : -EAGAIN;
}

int pouch_msgq_get(pouch_msgq_t *msgq, void *buf, int32_t timeout_ms)
{
    int result = k_msgq_get(msgq,
                            buf,
                            (0 < timeout_ms)        ? K_MSEC(timeout_ms)
                                : (timeout_ms == 0) ? K_NO_WAIT
                                                    : K_FOREVER);

    return (0 == result) ? 0 : -ENOMSG;
}

/*--------------------------------------------------
 * Miscellaneous
 *------------------------------------------------*/

void pouch_yield(void)
{
    k_yield();
}

/*--------------------------------------------------
 * Mutex
 *------------------------------------------------*/

/* Note: the underlying pouch_mutex_internal_t is defined in
 * /port/zephyr/include/pouch_port_macros_internal.h
 */

void pouch_mutex_init(pouch_mutex_t *mutex)
{
    k_mutex_init(mutex);
}

bool pouch_mutex_lock(pouch_mutex_t *mutex, int32_t timeout_ms)
{
    int ret = k_mutex_lock(mutex,
                           (timeout_ms > 0)        ? K_MSEC(timeout_ms)
                               : (0 == timeout_ms) ? K_NO_WAIT
                                                   : K_FOREVER);
    return (0 == ret) ? true : false;
}

bool pouch_mutex_unlock(pouch_mutex_t *mutex)
{
    int ret = k_mutex_unlock(mutex);
    return (0 == ret) ? true : false;
}

/*--------------------------------------------------
 * Work Queue
 *------------------------------------------------*/

void pouch_work_init(pouch_work_t *work, pouch_work_handler_t handler)
{
    k_work_init(work, handler);
}

void pouch_work_queue_init(pouch_work_q_t *queue)
{
    k_work_queue_init(queue);
}

void pouch_work_queue_start(pouch_work_q_t *queue,
                            void *stack,
                            size_t stack_size,
                            int prio,
                            char *name)
{
    struct k_work_queue_config workq_config = {.name = name};
    k_work_queue_start(queue, stack, stack_size, prio, &workq_config);
}

int pouch_work_submit_to_queue(pouch_work_q_t *queue, pouch_work_t *work)
{
    int ret = k_work_submit_to_queue(queue, work);

    /*
     * Already in queue:
     * ret == 0, return 0
     *
     * Added to queue:
     * ret == 1, was added to queue, return 1
     * ret == 2, handler is running, work was requeue, return 1
     *
     * Not added to queue:
     * ret < 0, return ret error code
     */
    return (0 == ret) ? 0 : (0 > ret) ? ret : 1;
}
