/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <pouch/port.h>
#include <stdbool.h>
#include <zephyr/kernel.h>

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
