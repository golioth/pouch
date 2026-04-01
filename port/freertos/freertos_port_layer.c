/*
 * Copyright (c) 2015-2016, Intel Corporation.
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "freertos/FreeRTOS.h"
#include <pouch/port.h>
#include <stdint.h>

POUCH_LOG_REGISTER(esp - idf - port - layer, POUCH_LOG_LEVEL_DBG);

/*--------------------------------------------------
 * Atomic
 *------------------------------------------------*/

#include "freertos/atomic.h"

/*
 * The Pouch FreeRTOS port currently only supports atomic operations on 32-bit processors
 *
 * This limitation is due to the FreeRTOS atomic_t being defined as uint32_t while the
 * pouch_atomic_t is intptr_t. If the platform is 64-bits, the underlying FreeRTOS atomic_t
 * remains 32-bits, while pouch_port_t would be 64-bits. (The same is true for 16-bit MCUs.) The
 * bit-width of these types must match to accomodate rollover and negative numbers.
 *
 * We have chosen intptr_t for pouch_port_t because it supports the Zephyr atomic_t (which is
 * defined as a long) for all bit-width systems.
 */
POUCH_STATIC_ASSERT(sizeof(pouch_atomic_t) <= sizeof(uint32_t),
                    "FreeRTOS atomic port requires pouch_atomic_t <= 32 bits");

/* Internal helper macros */
#define FREERTOS_ATOMIC_MASK(bit) BIT((intptr_t) (bit) & (POUCH_ATOMIC_BITS - 1U))
#define FREERTOS_ATOMIC_ELEM(addr, bit) ((addr) + ((bit) / POUCH_ATOMIC_BITS))


pouch_atomic_t pouch_atomic_dec(pouch_atomic_t *target)
{
    return (pouch_atomic_t) Atomic_Decrement_u32((uint32_t volatile *) target);
}

pouch_atomic_t pouch_atomic_inc(pouch_atomic_t *target)
{
    return (pouch_atomic_t) Atomic_Increment_u32((uint32_t volatile *) target);
}

pouch_atomic_t pouch_atomic_get(const pouch_atomic_t *target)
{
    /* FreeRTOS lacks an atomic get function. However we can use the OR fuction with 0       so that
     * no bits are altered and return the original value.
     */
    return (pouch_atomic_t) Atomic_OR_u32((uint32_t volatile *) target, 0);
}

pouch_atomic_t pouch_atomic_clear(pouch_atomic_t *target)
{
    /* FreeRTOS lacks an atomic clear function. However, we can use the AND function with 0 to clear
     * all bits and return the original value.
     */
    return (pouch_atomic_t) Atomic_AND_u32((uint32_t volatile *) target, 0);
}

pouch_atomic_t pouch_atomic_set(pouch_atomic_t *target, pouch_atomic_t value)
{
    return (pouch_atomic_t) Atomic_SwapPointers_p32((void *volatile *) target, (void *) value);
}

void pouch_atomic_clear_bit(pouch_atomic_t *target, int bit)
{
    pouch_atomic_t *elem = FREERTOS_ATOMIC_ELEM(target, bit);
    pouch_atomic_t mask = FREERTOS_ATOMIC_MASK(bit);

    Atomic_AND_u32((uint32_t volatile *) elem, (uint32_t) (~mask));
}

void pouch_atomic_set_bit(pouch_atomic_t *target, int bit)
{
    pouch_atomic_t *elem = FREERTOS_ATOMIC_ELEM(target, bit);
    pouch_atomic_t mask = FREERTOS_ATOMIC_MASK(bit);

    Atomic_OR_u32((uint32_t volatile *) elem, (uint32_t) mask);
}

bool pouch_atomic_test_bit(const pouch_atomic_t *target, int bit)
{
    const pouch_atomic_t *elem = FREERTOS_ATOMIC_ELEM(target, bit);
    pouch_atomic_t mask = FREERTOS_ATOMIC_MASK(bit);

    /* FreeRTOS lacks an atomic test bit function. However we can use the OR fuction with 0 so that
     * no bits are altered, then test the original value againt the desired bit to return bool.
     */
    uint32_t original_val = Atomic_OR_u32((uint32_t volatile *) elem, 0);
    return (0 != (original_val & mask));
}

bool pouch_atomic_test_and_clear_bit(pouch_atomic_t *target, int bit)
{
    const pouch_atomic_t *elem = FREERTOS_ATOMIC_ELEM(target, bit);
    pouch_atomic_t mask = FREERTOS_ATOMIC_MASK(bit);

    /* FreeRTOS lacks an atomic clear bit function. However, we can use the AND function with a bit
     * mask to clear the desired bit, then test the original value against the desired bit to return
     * bool.
     */
    uint32_t original_val = Atomic_AND_u32((uint32_t volatile *) elem, ~mask);
    return (0 != (original_val & mask));
}

bool pouch_atomic_test_and_set_bit(pouch_atomic_t *target, int bit)
{
    const pouch_atomic_t *elem = FREERTOS_ATOMIC_ELEM(target, bit);
    pouch_atomic_t mask = FREERTOS_ATOMIC_MASK(bit);

    /* FreeRTOS lacks an atomic set bit function. However, we can use the OR function with a bit
     * mask to set the desired bit, then test the original value against the desired bit to return
     * bool.
     */
    uint32_t original_val = Atomic_OR_u32((uint32_t volatile *) elem, mask);
    return (0 != (original_val & mask));
}

/*--------------------------------------------------
 * Big Endian
 *------------------------------------------------*/

/**
 * Big Endian implementation based on:
 * https://github.com/zephyrproject-rtos/zephyr/blob/v4.3.0/include/zephyr/sys/byteorder.h
 */

uint16_t pouch_get_be16(const uint8_t src[2])
{
    return ((uint16_t) src[0] << 8) | src[1];
}

uint32_t pouch_get_be32(const uint8_t src[4])
{
    return ((uint32_t) pouch_get_be16(&src[0]) << 16) | pouch_get_be16(&src[2]);
}

uint64_t pouch_get_be64(const uint8_t src[8])
{
    return ((uint64_t) pouch_get_be32(&src[0]) << 32) | pouch_get_be32(&src[4]);
}

void pouch_put_be16(uint16_t val, uint8_t dst[2])
{
    dst[0] = val >> 8;
    dst[1] = val;
}
