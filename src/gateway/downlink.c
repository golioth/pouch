/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <pouch/blockbuf.h>
#include "downlink.h"
#include <pouch/port.h>

#include "../buf.h"

POUCH_LOG_REGISTER(gw_downlink, CONFIG_POUCH_GATEWAY_LOG_LEVEL);

enum
{
    DOWNLINK_FLAG_COMPLETE,
    DOWNLINK_FLAG_TRANSPORT_ABORTED,
    DOWNLINK_FLAG_TRANSPORT_WAITING,
    DOWNLINK_FLAG_COAP_ERROR,
    DOWNLINK_FLAG_COUNT,
};

struct pouch_gateway_downlink_context
{
    pouch_gateway_downlink_data_available_cb data_available_cb;
    void *cb_arg;

    pouch_msgq_t block_queue;
    uint8_t block_queue_buf[CONFIG_POUCH_GATEWAY_NUM_BLOCKS * sizeof(struct pouch_buf *)];

    /* Currently being drained.  Owned by the consumer thread. */
    struct pouch_buf *current_block;
    /* Offset into current_block for the next byte to read. */
    size_t offset;
    /* The block that was flagged as last by the producer.  Compared
     * against current_block on consumption to determine end-of-stream.
     * Single-writer (producer), single-reader (consumer); the
     * happens-before across the msgq put/get makes this safe.
     */
    struct pouch_buf *last_block;

    POUCH_ATOMIC_DEFINE(flags, DOWNLINK_FLAG_COUNT);
};

static void flush_block_queue(struct pouch_gateway_downlink_context *downlink)
{
    struct pouch_buf *block;

    while (pouch_msgq_get(&downlink->block_queue, &block, POUCH_NO_WAIT) == 0)
    {
        blockbuf_free(block);
    }
}

int pouch_gateway_downlink_block_cb(const uint8_t *data, size_t len, bool is_last, void *arg)
{
    struct pouch_gateway_downlink_context *downlink = arg;

    if (pouch_atomic_test_bit(downlink->flags, DOWNLINK_FLAG_TRANSPORT_ABORTED))
    {
        return -ECANCELED;
    }

    pouch_timepoint_t deadline =
        pouch_timepoint_get(POUCH_SECONDS(CONFIG_POUCH_GATEWAY_DOWNLINK_BLOCK_TIMEOUT));

    struct pouch_buf *block = blockbuf_alloc(pouch_timepoint_timeout(deadline));
    if (block == NULL)
    {
        POUCH_LOG_ERR("Failed to allocate block");
        return -ENOMEM;
    }

    buf_write(block, data, len);

    /* Record the last block's pointer BEFORE submitting so the
     * consumer sees it as soon as it dequeues this buffer.
     */
    if (is_last)
    {
        downlink->last_block = block;
    }

    int err = pouch_msgq_put(&downlink->block_queue, &block, pouch_timepoint_timeout(deadline));
    if (err)
    {
        POUCH_LOG_ERR("Failed to enqueue block: %d", err);
        if (is_last)
        {
            downlink->last_block = NULL;
        }
        blockbuf_free(block);
        return -ENOMEM;
    }

    if (NULL == downlink->current_block
        && pouch_atomic_test_and_clear_bit(downlink->flags, DOWNLINK_FLAG_TRANSPORT_WAITING))
    {
        downlink->data_available_cb(downlink->cb_arg);
    }

    return 0;
}

void pouch_gateway_downlink_end_cb(int status, void *arg)
{
    struct pouch_gateway_downlink_context *downlink = arg;

    if (0 != status)
    {
        POUCH_LOG_ERR("Downlink ending due to error %d", status);

        if (pouch_atomic_test_bit(downlink->flags, DOWNLINK_FLAG_TRANSPORT_ABORTED))
        {
            pouch_gateway_downlink_close(downlink);
        }
        else
        {
            pouch_atomic_set_bit(downlink->flags, DOWNLINK_FLAG_COAP_ERROR);

            if (NULL == downlink->current_block
                && pouch_atomic_test_and_clear_bit(downlink->flags,
                                                   DOWNLINK_FLAG_TRANSPORT_WAITING))
            {
                downlink->data_available_cb(downlink->cb_arg);
            }
        }
    }
}

struct pouch_gateway_downlink_context *pouch_gateway_downlink_open(
    pouch_gateway_downlink_data_available_cb data_available_cb,
    void *cb_arg)
{
    POUCH_LOG_INF("Starting downlink");

    struct pouch_gateway_downlink_context *downlink =
        malloc(sizeof(struct pouch_gateway_downlink_context));
    if (downlink == NULL)
    {
        return NULL;
    }

    downlink->data_available_cb = data_available_cb;
    downlink->cb_arg = cb_arg;
    downlink->current_block = NULL;
    downlink->last_block = NULL;
    downlink->offset = 0;
    pouch_atomic_clear_bit(downlink->flags, DOWNLINK_FLAG_COMPLETE);
    pouch_atomic_clear_bit(downlink->flags, DOWNLINK_FLAG_TRANSPORT_ABORTED);
    pouch_atomic_clear_bit(downlink->flags, DOWNLINK_FLAG_COAP_ERROR);
    pouch_atomic_set_bit(downlink->flags, DOWNLINK_FLAG_TRANSPORT_WAITING);

    pouch_msgq_init(&downlink->block_queue,
                    downlink->block_queue_buf,
                    sizeof(downlink->block_queue_buf),
                    sizeof(struct pouch_buf *));

    return downlink;
}

int pouch_gateway_downlink_get_data(struct pouch_gateway_downlink_context *downlink,
                                    void *dst,
                                    size_t *dst_len,
                                    bool *is_last)
{
    *is_last = false;

    if (pouch_gateway_downlink_is_complete(downlink))
    {
        return -ENODATA;
    }

    size_t total_bytes_copied = 0;

    while (*dst_len)
    {
        if (NULL == downlink->current_block)
        {
            if (pouch_msgq_get(&downlink->block_queue, &downlink->current_block, POUCH_NO_WAIT)
                != 0)
            {
                *dst_len = total_bytes_copied;
                if (pouch_atomic_test_bit(downlink->flags, DOWNLINK_FLAG_COAP_ERROR))
                {
                    *is_last = true;
                    pouch_atomic_set_bit(downlink->flags, DOWNLINK_FLAG_COMPLETE);
                    return 0;
                }
                if (0 == total_bytes_copied)
                {
                    pouch_atomic_set_bit(downlink->flags, DOWNLINK_FLAG_TRANSPORT_WAITING);
                }
                return 0;
            }
        }

        struct pouch_bufview v;
        pouch_bufview_init(&v, downlink->current_block);
        v.offset = downlink->offset;

        size_t bytes_to_copy = MIN(*dst_len, pouch_bufview_available(&v));
        pouch_bufview_memcpy(&v, dst, bytes_to_copy);

        downlink->offset += bytes_to_copy;
        *dst_len -= bytes_to_copy;
        dst = (void *) ((intptr_t) dst + bytes_to_copy);
        total_bytes_copied += bytes_to_copy;

        if (buf_size_get(downlink->current_block) == downlink->offset)
        {
            bool drained_last = (downlink->current_block == downlink->last_block);

            blockbuf_free(downlink->current_block);
            downlink->offset = 0;

            if (pouch_msgq_get(&downlink->block_queue, &downlink->current_block, POUCH_NO_WAIT)
                != 0)
            {
                downlink->current_block = NULL;
            }

            if (drained_last)
            {
                pouch_atomic_set_bit(downlink->flags, DOWNLINK_FLAG_COMPLETE);
                *is_last = true;
                break;
            }
        }
    }

    *dst_len = total_bytes_copied;
    return 0;
}

bool pouch_gateway_downlink_is_complete(const struct pouch_gateway_downlink_context *downlink)
{
    return pouch_atomic_test_bit(downlink->flags, DOWNLINK_FLAG_COMPLETE);
}

void pouch_gateway_downlink_close(struct pouch_gateway_downlink_context *downlink)
{
    flush_block_queue(downlink);

    if (NULL != downlink->current_block)
    {
        blockbuf_free(downlink->current_block);
    }

    free(downlink);
}

void pouch_gateway_downlink_abort(struct pouch_gateway_downlink_context *downlink)
{
    POUCH_LOG_INF("Aborting downlink");

    pouch_atomic_set_bit(downlink->flags, DOWNLINK_FLAG_TRANSPORT_ABORTED);

    if (pouch_gateway_downlink_is_complete(downlink))
    {
        pouch_gateway_downlink_close(downlink);
    }
}
