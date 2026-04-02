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
