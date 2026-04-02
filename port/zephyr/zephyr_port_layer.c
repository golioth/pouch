/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <pouch/port.h>
#include <stdbool.h>
#include <stdint.h>

/*--------------------------------------------------
 * Atomic
 *------------------------------------------------*/

#include <zephyr/sys/atomic.h>

#define POUCH_ATOMIC_DEFINE_INTERNAL(i) ATOMIC_DEFINE(i)

pouch_atomic_t pouch_atomic_dec(pouch_atomic_t *target)
{
    return (pouch_atomic_t) atomic_dec((atomic_t *) target);
}

pouch_atomic_t pouch_atomic_inc(pouch_atomic_t *target)
{
    return (pouch_atomic_t) atomic_inc((atomic_t *) target);
}

pouch_atomic_t pouch_atomic_get(const pouch_atomic_t *target)
{
    return (pouch_atomic_t) atomic_get((atomic_t *) target);
}

pouch_atomic_t pouch_atomic_clear(pouch_atomic_t *target)
{
    return (pouch_atomic_t) atomic_clear((atomic_t *) target);
}

pouch_atomic_t pouch_atomic_set(pouch_atomic_t *target, pouch_atomic_t value)
{
    return (pouch_atomic_t) atomic_set((atomic_t *) target, (atomic_t) value);
}

void pouch_atomic_clear_bit(pouch_atomic_t *target, int bit)
{
    atomic_clear_bit((atomic_t *) target, bit);
}

void pouch_atomic_set_bit(pouch_atomic_t *target, int bit)
{
    atomic_set_bit((atomic_t *) target, bit);
}

bool pouch_atomic_test_bit(const pouch_atomic_t *target, int bit)
{
    return atomic_test_bit((const atomic_t *) target, bit);
}

bool pouch_atomic_test_and_clear_bit(pouch_atomic_t *target, int bit)
{
    return atomic_test_and_clear_bit((atomic_t *) target, bit);
}

bool pouch_atomic_test_and_set_bit(pouch_atomic_t *target, int bit)
{
    return atomic_test_and_set_bit((atomic_t *) target, bit);
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
