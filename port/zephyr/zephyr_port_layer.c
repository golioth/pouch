/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

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
