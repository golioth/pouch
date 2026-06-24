/*
 * Copyright (c) 2015-2016, Intel Corporation.
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "errno.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <pouch/port.h>
#include "freertos_port_layer.h"
#include <stdint.h>
#include <string.h>

POUCH_LOG_REGISTER(esp_idf_port_layer, POUCH_LOG_LEVEL_DBG);

/*--------------------------------------------------
 * Atomic
 *------------------------------------------------*/

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "freertos/atomic.h"
#pragma GCC diagnostic pop

/*
 * The FreeRTOS atomic implementation uses uint32_t for the type. The pouch_atomic_t type matches
 * this by setting pouch_atomic_internalt_t to uint32_t in freertos_port_layer.h.
 *
 * The pouch_atomic_* API returns long for atomic values. For casting to work, the bit-width of
 * uint32_t must but be equal to the bit-width of long. This will likely only be true for 32-bit
 * processors. This compile-time assert assures these types are of equal size.
 */
POUCH_STATIC_ASSERT(sizeof(pouch_atomic_t) == sizeof(long),
                    "Pouch atomic port API requires sizeof(pouch_atomic_t) == sizeof(long)");

/* Internal helper macros */
#define FREERTOS_ATOMIC_MASK(bit) BIT((intptr_t) (bit) & (POUCH_ATOMIC_BITS - 1U))
#define FREERTOS_ATOMIC_ELEM(addr, bit) ((addr) + ((bit) / POUCH_ATOMIC_BITS))


long pouch_atomic_dec(pouch_atomic_t *target)
{
    return (long) Atomic_Decrement_u32(target);
}

long pouch_atomic_inc(pouch_atomic_t *target)
{
    return (long) Atomic_Increment_u32(target);
}

long pouch_atomic_get_value(const pouch_atomic_t *target)
{
    /* FreeRTOS lacks an atomic get function. However we can use the OR fuction with 0 so that no
     * bits are altered and return the original value.
     */
    return (long) Atomic_OR_u32((uint32_t volatile *) target, 0);
}

long pouch_atomic_clear(pouch_atomic_t *target)
{
    /* FreeRTOS lacks an atomic clear function. However, we can use the AND function with 0 to clear
     * all bits and return the original value.
     */
    return (long) Atomic_AND_u32(target, 0);
}

long pouch_atomic_set(pouch_atomic_t *target, long value)
{
    return (long) Atomic_SwapPointers_p32((void *volatile *) target, (void *) value);
}

void pouch_atomic_clear_bit(pouch_atomic_t *target, int bit)
{
    pouch_atomic_t *elem = FREERTOS_ATOMIC_ELEM(target, bit);
    pouch_atomic_t mask = FREERTOS_ATOMIC_MASK(bit);

    Atomic_AND_u32(elem, (uint32_t) (~mask));
}

void pouch_atomic_set_bit(pouch_atomic_t *target, int bit)
{
    pouch_atomic_t *elem = FREERTOS_ATOMIC_ELEM(target, bit);
    pouch_atomic_t mask = FREERTOS_ATOMIC_MASK(bit);

    Atomic_OR_u32(elem, (uint32_t) mask);
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

/*------------------------------------------------
 * Time
 *------------------------------------------------*/

int32_t pouch_timeout_to_freertos_ticks(int32_t pouch_timeout)
{
    if (pouch_timeout > 0)
    {
        return pdMS_TO_TICKS(pouch_timeout);
    }

    if (0 == pouch_timeout)
    {
        return 0;
    }

    return portMAX_DELAY;
}

pouch_timepoint_t pouch_timepoint_get(pouch_timeout_t timeout)
{
    if (timeout > 0)
    {
        uint32_t now = xTaskGetTickCount();
        return now + pdMS_TO_TICKS(timeout);
    }

    return timeout;
}

pouch_timeout_t pouch_timepoint_timeout(pouch_timepoint_t tp)
{
    if (tp > 0)
    {
        uint32_t now = xTaskGetTickCount();
        if (tp <= now)
        {
            return 0;
        }

        return tp - now;
    }
    return tp;
}

/*--------------------------------------------------
 * Linked List
 *------------------------------------------------*/

#include "freertos/list.h"

void pouch_slist_init(pouch_slist_t *list)
{
    vListInitialise(list);
}

void pouch_slist_node_init(pouch_slist_node_t *node)
{
    vListInitialiseItem(node);
}

void pouch_slist_append(pouch_slist_t *list, pouch_slist_node_t *node)
{
    vListInsertEnd(list, node);
}

static pouch_slist_node_t *get_head_node(pouch_slist_t *list)
{
    /* vListInitialise() adds an end of list marker. We must check for an empty list before
     * fetching the head pointer or else we will get a pointer to the end marker. */
    if (pdTRUE == listLIST_IS_EMPTY(list))
    {
        return NULL;
    }

    return listGET_HEAD_ENTRY(list);
}

pouch_slist_node_t *pouch_slist_get(pouch_slist_t *list)
{
    pouch_slist_node_t *head = get_head_node(list);

    if (NULL != head)
    {
        uxListRemove(head);
    }

    return head;
}

pouch_slist_node_t *pouch_slist_peek_head(pouch_slist_t *list)
{
    return get_head_node(list);
}

/*--------------------------------------------------
 * Message Queue
 *------------------------------------------------*/

#include "freertos/queue.h"

void pouch_msgq_init(pouch_msgq_t *msgq,
                     uint8_t *msgq_buffer,
                     size_t msgq_buffer_size,
                     size_t msg_size)
{
    msgq->xQueue =
        xQueueCreateStatic(msgq_buffer_size / msg_size, msg_size, msgq_buffer, &msgq->xStaticQueue);
}

int pouch_msgq_put(pouch_msgq_t *msgq, const void *data, pouch_timeout_t timeout)
{
    bool result = xQueueSend(msgq->xQueue, data, pouch_timeout_to_freertos_ticks(timeout));

    return (pdPASS == result) ? 0 : -EAGAIN;
}

int pouch_msgq_get(pouch_msgq_t *msgq, void *buf, pouch_timeout_t timeout)
{
    bool result = xQueueReceive(msgq->xQueue, buf, pouch_timeout_to_freertos_ticks(timeout));

    return (pdPASS == result) ? 0 : -ENOMSG;
}

/*--------------------------------------------------
 * Miscellaneous
 *------------------------------------------------*/

void pouch_yield(void)
{
    taskYIELD();
}

/*--------------------------------------------------
 * Mutex
 *------------------------------------------------*/

/* Note: the underlying pouch_mutex_internal_t is defined in
 * /port/freertos/freertos_port_layer.h
 */

void pouch_mutex_init(pouch_mutex_t *mutex)
{
    *mutex = xSemaphoreCreateMutex();
}

bool pouch_mutex_lock(pouch_mutex_t *mutex, pouch_timeout_t timeout)
{
    return xSemaphoreTake(*mutex, pouch_timeout_to_freertos_ticks(timeout));
}

bool pouch_mutex_unlock(pouch_mutex_t *mutex)
{
    return xSemaphoreGive(*mutex);
}

/*--------------------------------------------------
 * Semaphore
 *------------------------------------------------*/

int pouch_sem_init(pouch_sem_t *sem, unsigned int initial_count, unsigned int limit)
{
    sem->handle = xSemaphoreCreateCountingStatic(limit, initial_count, &sem->buf);
    configASSERT(sem->handle != NULL);
    return 0;
}

int pouch_sem_take(pouch_sem_t *sem, pouch_timeout_t timeout)
{
    bool success = xSemaphoreTake(sem->handle, timeout);
    return (pdTRUE == success) ? 0 : -EBUSY;
}

void pouch_sem_give(pouch_sem_t *sem)
{
    xSemaphoreGive(sem->handle);
}

void pouch_sem_reset(pouch_sem_t *sem)
{
    int success = 0;

    while (success == 0)
    {
        success = pouch_sem_take(sem, POUCH_NO_WAIT);
    }
}

/*--------------------------------------------------
 * Work Queue
 *------------------------------------------------*/

static void work_q_handler_proxy(void *param)
{
    pouch_work_t *work;
    pouch_work_q_t *q = (pouch_work_q_t *) param;

    while (true)
    {
        if (pdTRUE == xQueueReceive(q->items, &work, portMAX_DELAY))
        {
            /* Clear flag immediately; then handler can re-queue work if it wants */
            pouch_atomic_clear_bit(&work->flags, POUCH_FREERTOS_WORK_FLAG_QUEUED);

            if (work && work->handler)
            {
                work->handler(work);
            }

            pouch_yield();
        }
    }
}

void pouch_work_init(pouch_work_t *work, pouch_work_handler_t handler)
{
    pouch_atomic_clear(&work->flags);
    work->handler = handler;
}

void pouch_work_queue_init(pouch_work_q_t *queue)
{
    queue->items = xQueueCreateStatic(sizeof(queue->priv_storage) / sizeof(pouch_work_t *),
                                      sizeof(pouch_work_t *),
                                      queue->priv_storage,
                                      &queue->priv_static_queue);

    configASSERT(NULL != queue->items);
}

void pouch_work_queue_start(pouch_work_q_t *queue,
                            void *stack,
                            size_t stack_size,
                            int prio,
                            char *name)
{
    queue->handle = xTaskCreateStatic(work_q_handler_proxy,
                                      name,
                                      stack_size / sizeof(StackType_t),
                                      queue,
                                      prio,
                                      stack,
                                      &queue->priv_task_buf);

    configASSERT(queue->handle != NULL);
}

int pouch_work_submit_to_queue(pouch_work_q_t *queue, pouch_work_t *work)
{
    if (true == pouch_atomic_test_and_set_bit(&work->flags, POUCH_FREERTOS_WORK_FLAG_QUEUED))
    {
        /* Work already in queue */
        return 0;
    }

    if (xQueueSend(queue->items, &work, 0) == pdTRUE)
    {
        /* Work  added to queue */
        return 0;
    }

    /* Failed to add work to queue */
    /* Clear pending bit that was set earlier in this function */
    pouch_atomic_clear_bit(&work->flags, POUCH_FREERTOS_WORK_FLAG_QUEUED);
    return -ENOMEM;
}

struct work_flush_sync
{
    pouch_work_t work;
    pouch_sem_t sem;
};

static void work_flush_sentinel_handler(pouch_work_t *work)
{
    struct work_flush_sync *sync = CONTAINER_OF(work, struct work_flush_sync, work);

    pouch_sem_give(&sync->sem);
}

void pouch_work_queue_flush(pouch_work_q_t *queue)
{
    struct work_flush_sync sync;

    pouch_work_init(&sync.work, work_flush_sentinel_handler);
    pouch_sem_init(&sync.sem, 0, 1);

    if (0 == pouch_work_submit_to_queue(queue, &sync.work))
    {
        pouch_sem_take(&sync.sem, POUCH_FOREVER);
    }
}

/*--------------------------------------------------
 * Delayable Work
 *------------------------------------------------*/

#if defined(CONFIG_POUCH_DELAYABLE_WORK)

static StackType_t dwork_stack[CONFIG_POUCH_DELAYABLE_WORK_STACK_SIZE / sizeof(StackType_t)];
static StaticTask_t dwork_task_buf;
static TaskHandle_t dwork_task_handle;
static QueueHandle_t dwork_queue;
static StaticQueue_t dwork_queue_buf;
static uint8_t
    dwork_queue_storage[CONFIG_POUCH_DELAYABLE_WORK_Q_SIZE * sizeof(pouch_work_delayable_t *)];

static void dwork_task_fn(void *param)
{
    pouch_work_delayable_t *dwork;

    while (true)
    {
        if (pdTRUE == xQueueReceive(dwork_queue, &dwork, portMAX_DELAY))
        {
            if ((NULL != dwork) && (NULL != dwork->handler))
            {
                dwork->handler(dwork);
            }
        }
    }
}

static void dwork_queue_init(void)
{
    if (NULL != dwork_task_handle)
    {
        return;
    }

    dwork_queue = xQueueCreateStatic(CONFIG_POUCH_DELAYABLE_WORK_Q_SIZE,
                                     sizeof(pouch_work_delayable_t *),
                                     dwork_queue_storage,
                                     &dwork_queue_buf);
    configASSERT(NULL != dwork_queue);

    dwork_task_handle =
        xTaskCreateStatic(dwork_task_fn,
                          "pouch_dwork",
                          CONFIG_POUCH_DELAYABLE_WORK_STACK_SIZE / sizeof(StackType_t),
                          NULL,
                          CONFIG_POUCH_DELAYABLE_WORK_PRIORITY,
                          dwork_stack,
                          &dwork_task_buf);
    configASSERT(NULL != dwork_task_handle);
}

static void delayable_work_timer_cb(TimerHandle_t timer)
{
    pouch_work_delayable_t *dwork = (pouch_work_delayable_t *) pvTimerGetTimerID(timer);

    if (NULL != dwork)
    {
        xQueueSend(dwork_queue, &dwork, portMAX_DELAY);
    }
}

void pouch_work_delayable_init(pouch_work_delayable_t *dwork,
                               pouch_work_delayable_handler_t handler)
{
    dwork->handler = handler;
    dwork->timer = xTimerCreateStatic("pouch_dwork",
                                      1,       /* dummy period, changed on schedule */
                                      pdFALSE, /* one-shot */
                                      dwork,   /* timer ID = dwork pointer */
                                      delayable_work_timer_cb,
                                      &dwork->timer_buf);
    configASSERT(NULL != dwork->timer);

    dwork_queue_init();
}

static int do_work_schedule(pouch_work_delayable_t *dwork, pouch_timeout_t delay)
{
    if (POUCH_NO_WAIT == delay)
    {
        /* Submit to work queue for immediate processing */
        xQueueSend(dwork_queue, &dwork, portMAX_DELAY);
        return 0;
    }

    TickType_t ticks = pdMS_TO_TICKS(delay);
    if (ticks == 0)
    {
        ticks = 1; /* Minimum 1 tick */
    }

    xTimerChangePeriod(dwork->timer, ticks, portMAX_DELAY);
    xTimerStart(dwork->timer, portMAX_DELAY);
    return 0;
}

int pouch_work_schedule(pouch_work_delayable_t *dwork, pouch_timeout_t delay)
{
    if (xTimerIsTimerActive(dwork->timer))
    {
        /* Already scheduled, do nothing */
        return 0;
    }

    return do_work_schedule(dwork, delay);
}

int pouch_work_reschedule(pouch_work_delayable_t *dwork, pouch_timeout_t delay)
{
    xTimerStop(dwork->timer, portMAX_DELAY);
    return do_work_schedule(dwork, delay);
}

/*
 * Note: Unlike Zephyr's k_work_cancel_delayable() which also removes work from
 * the queue if it has been submitted but not yet executed, this implementation
 * only cancels the pending timer. If the timer has already fired and the work
 * item is sitting in the queue awaiting execution, it will still run. In
 * practice this means a handler may execute once after cancel in the narrow
 * window between timer expiry and task processing.
 */
void pouch_work_cancel_delayable(pouch_work_delayable_t *dwork)
{
    xTimerStop(dwork->timer, portMAX_DELAY);
}

#endif /* CONFIG_POUCH_DELAYABLE_WORK */
