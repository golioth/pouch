/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "freertos/FreeRTOS.h"

/*--------------------------------------------------
 * Mutex
 *------------------------------------------------*/

#include "freertos/semphr.h"

typedef SemaphoreHandle_t pouch_mutex_internal_t;

#define POUCH_MUTEX_DEFINE_INTERNAL(name)                         \
    SemaphoreHandle_t name = NULL;                                \
    StaticSemaphore_t xMutexBuffer_##name;                        \
                                                                  \
    static void static_mutex_init_##name(void)                    \
    {                                                             \
        name = xSemaphoreCreateMutexStatic(&xMutexBuffer_##name); \
        configASSERT(name != NULL);                               \
    }                                                             \
    POUCH_APPLICATION_STARTUP_HOOK(static_mutex_init_##name)
