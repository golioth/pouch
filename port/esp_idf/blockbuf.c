/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <pouch/blockbuf.h>
#include <pouch/port.h>
#include "../../src/block.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define BLOCKBUF_ALIGN 4
#define BLOCKBUF_BLOCK_SIZE \
    ROUND_UP((POUCH_BUF_OVERHEAD + MAX_PLAINTEXT_BLOCK_SIZE), BLOCKBUF_ALIGN)

static struct pouch_buf *_blockbuf_pool[CONFIG_POUCH_BLOCK_COUNT];
static uint8_t _blockbuf_storage[CONFIG_POUCH_BLOCK_COUNT][BLOCKBUF_BLOCK_SIZE]
    __attribute__((aligned(BLOCKBUF_ALIGN)));

static StaticQueue_t _blockbuf_queue_buf;
static QueueHandle_t _blockbuf_queue_handle;

static void blockbuf_init(void)
{
    _blockbuf_queue_handle = xQueueCreateStatic(CONFIG_POUCH_BLOCK_COUNT,
                                                sizeof(struct pouch_buf *),
                                                (uint8_t *) _blockbuf_pool,
                                                &_blockbuf_queue_buf);
    configASSERT(_blockbuf_queue_handle != NULL);

    for (int i = 0; i < CONFIG_POUCH_BLOCK_COUNT; i++)
    {
        struct pouch_buf *buf = (struct pouch_buf *) _blockbuf_storage[i];
        BaseType_t err = xQueueSend(_blockbuf_queue_handle, &buf, 0);
        configASSERT(err == pdPASS);
    }
}
POUCH_APPLICATION_STARTUP_HOOK(blockbuf_init);

struct pouch_buf *blockbuf_alloc(pouch_timeout_t timeout)
{
    struct pouch_buf *buf = NULL;

    if (pdTRUE
        != xQueueReceive(_blockbuf_queue_handle, &buf, pouch_timeout_to_freertos_ticks(timeout)))
    {
        return NULL;
    }

    buf_init(buf);
    return buf;
}

void blockbuf_free(struct pouch_buf *buf)
{
    if (buf == NULL)
    {
        return;
    }

    BaseType_t err = xQueueSend(_blockbuf_queue_handle, &buf, 0);
    /* This can only return errQUEUE_FULL which should never happen */
    configASSERT(pdPASS == err);
}
