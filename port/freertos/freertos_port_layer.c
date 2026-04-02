/*
 * Copyright (c) 2015-2016, Intel Corporation.
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "freertos/FreeRTOS.h"
#include "freertos_port_layer.h"
#include <pouch/port.h>
#include <stdint.h>

/*--------------------------------------------------
 * Big Endian
 *------------------------------------------------*/

/**
 * Big Endine implementation based on:
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

/*--------------------------------------------------
 * Mutex
 *------------------------------------------------*/

/* Note: the underlying pouch_mutex_internal_t is defined in
 * /port/freertos/freertos_port_layer.h
 */

#include "freertos/semphr.h"

void pouch_mutex_init(pouch_mutex_t *mutex)
{
    *mutex = xSemaphoreCreateMutex();
}

bool pouch_mutex_lock(pouch_mutex_t *mutex, int32_t timeout_ms)
{
    return xSemaphoreTake(*mutex,
                          (timeout_ms > 0)        ? pdMS_TO_TICKS(timeout_ms)
                              : (0 == timeout_ms) ? 0
                                                  : portMAX_DELAY);
}

bool pouch_mutex_unlock(pouch_mutex_t *mutex)
{
    return xSemaphoreGive(*mutex);
}
