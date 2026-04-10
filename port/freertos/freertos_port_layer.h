/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "freertos/FreeRTOS.h"
#include <pouch/port.h>
#include <stdint.h>
#include <errno.h> /* Needed to support for Linux-style error codes used in Pouch */

/*--------------------------------------------------
 * Atomic
 *------------------------------------------------*/

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "freertos/atomic.h"
#pragma GCC diagnostic pop

typedef uint32_t pouch_atomic_internal_t;

/* Forward declaration (used in Work Queue section) prevents circular dependency */
/* This must match what's in port.h */
typedef pouch_atomic_internal_t pouch_atomic_t;

/*--------------------------------------------------
 * Linked List
 *------------------------------------------------*/

#include "freertos/list.h"

typedef List_t pouch_slist_internal_t;

typedef ListItem_t pouch_slist_node_internal_t;

/*--------------------------------------------------
 * Message Queue
 *------------------------------------------------*/

struct freertos_msgq_ctx
{
    QueueHandle_t xQueue;
    StaticQueue_t xStaticQueue;
};

typedef struct freertos_msgq_ctx pouch_msgq_internal_t;

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

/*--------------------------------------------------
 * Work Queue
 *------------------------------------------------*/

#define POUCH_FREERTOS_WORK_Q_DEFAULT_QUEUE_LENGTH 10
#define POUCH_FREERTOS_WORK_FLAG_QUEUED 0

typedef struct pouch_freertos_work_q pouch_work_q_internal_t;
typedef struct pouch_freertos_work pouch_work_internal_t;

/* Forward declarations to avoid circular dependencies. These must match port.h */
typedef pouch_work_internal_t pouch_work_t;
typedef pouch_work_q_internal_t pouch_work_q_t;
typedef void (*pouch_work_handler_t)(pouch_work_t *work);

struct pouch_freertos_work_q
{
    TaskHandle_t handle;
    StaticTask_t priv_task_buf;

    QueueHandle_t items;
    StaticQueue_t priv_static_queue;
    uint8_t priv_storage[POUCH_FREERTOS_WORK_Q_DEFAULT_QUEUE_LENGTH * sizeof(pouch_work_t *)];
};

struct pouch_freertos_work
{
    pouch_atomic_t flags;
    pouch_work_handler_t handler;
};

#define POUCH_THREAD_STACK_DEFINE_INTERNAL(name, size) StackType_t name[size / sizeof(StackType_t)]
