/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#include <golioth/gateway.h>

#include "block.h"
#include <pouch/gateway/downlink.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(downlink, CONFIG_POUCH_GATEWAY_LOG_LEVEL);

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
    struct k_fifo block_queue;
    struct block *current_block;
    size_t offset;
    ATOMIC_DEFINE(flags, DOWNLINK_FLAG_COUNT);
};

static struct golioth_client *_client;

static void flush_block_queue(struct k_fifo *queue)
{
    struct block *block = k_fifo_get(queue, K_NO_WAIT);
    while (NULL != block)
    {
        block_free(block);

        block = k_fifo_get(queue, K_NO_WAIT);
    }
}

enum golioth_status pouch_gateway_downlink_block_cb(const uint8_t *data,
                                                    size_t len,
                                                    bool is_last,
                                                    void *arg)
{
    struct pouch_gateway_downlink_context *downlink = arg;

    if (atomic_test_bit(downlink->flags, DOWNLINK_FLAG_TRANSPORT_ABORTED))
    {
        return GOLIOTH_ERR_NACK;
    }

    struct block *block = block_alloc(NULL, K_SECONDS(CONFIG_POUCH_GATEWAY_DOWNLINK_BLOCK_TIMEOUT));
    if (NULL == block)
    {
        LOG_ERR("Failed to allocate block");
        return GOLIOTH_ERR_MEM_ALLOC;
    }

    block_append(block, data, len);

    if (is_last)
    {
        block_mark_last(block);
    }
    k_fifo_put(&downlink->block_queue, block);

    if (NULL == downlink->current_block
        && atomic_test_and_clear_bit(downlink->flags, DOWNLINK_FLAG_TRANSPORT_WAITING))
    {
        downlink->data_available_cb(downlink->cb_arg);
    }

    return GOLIOTH_OK;
}

void pouch_gateway_downlink_end_cb(enum golioth_status status,
                                   const struct golioth_coap_rsp_code *coap_rsp_code,
                                   void *arg)
{
    struct pouch_gateway_downlink_context *downlink = arg;

    if (GOLIOTH_OK != status)
    {
        LOG_ERR("Downlink ending due to error %d", status);
        if (GOLIOTH_ERR_COAP_RESPONSE == status)
        {
            LOG_ERR("CoAP error: %d.%02d", coap_rsp_code->code_class, coap_rsp_code->code_detail);
        }

        /* If transport already aborted, close downlink */

        if (atomic_test_bit(downlink->flags, DOWNLINK_FLAG_TRANSPORT_ABORTED))
        {
            pouch_gateway_downlink_close(downlink);
        }
        else
        {
            atomic_set_bit(downlink->flags, DOWNLINK_FLAG_COAP_ERROR);

            /* If transport is waiting for a block, kick it */

            if (NULL == downlink->current_block
                && atomic_test_and_clear_bit(downlink->flags, DOWNLINK_FLAG_TRANSPORT_WAITING))
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
    LOG_INF("Starting downlink");

    struct pouch_gateway_downlink_context *downlink =
        malloc(sizeof(struct pouch_gateway_downlink_context));

    if (NULL != downlink)
    {
        downlink->data_available_cb = data_available_cb;
        downlink->cb_arg = cb_arg;
        downlink->current_block = NULL;
        downlink->offset = 0;
        atomic_clear_bit(downlink->flags, DOWNLINK_FLAG_COMPLETE);
        atomic_clear_bit(downlink->flags, DOWNLINK_FLAG_TRANSPORT_ABORTED);
        atomic_clear_bit(downlink->flags, DOWNLINK_FLAG_COAP_ERROR);
        atomic_set_bit(downlink->flags, DOWNLINK_FLAG_TRANSPORT_WAITING);
        k_fifo_init(&downlink->block_queue);
    }

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
            downlink->current_block = k_fifo_get(&downlink->block_queue, K_NO_WAIT);
            if (NULL == downlink->current_block)
            {
                *dst_len = total_bytes_copied;
                if (atomic_test_bit(downlink->flags, DOWNLINK_FLAG_COAP_ERROR))
                {
                    /* We previously received a CoAP error and now the block queue is empty */
                    *is_last = true;
                    atomic_set_bit(downlink->flags, DOWNLINK_FLAG_COMPLETE);
                    return 0;
                }
                if (0 == *dst_len)
                {
                    /* We could not provide any data to the client, so we will
                       notify them the next time we receive a block */
                    atomic_set_bit(downlink->flags, DOWNLINK_FLAG_TRANSPORT_WAITING);
                }

                return 0;
            }
        }

        size_t bytes_to_copy =
            MIN(*dst_len, block_length(downlink->current_block) - downlink->offset);
        block_get(downlink->current_block, downlink->offset, dst, bytes_to_copy);

        downlink->offset += bytes_to_copy;
        *dst_len -= bytes_to_copy;
        dst = (void *) ((intptr_t) dst + bytes_to_copy);
        total_bytes_copied += bytes_to_copy;

        if (block_length(downlink->current_block) == downlink->offset)
        {
            *is_last = block_is_last(downlink->current_block);

            block_free(downlink->current_block);
            downlink->offset = 0;
            downlink->current_block = k_fifo_get(&downlink->block_queue, K_NO_WAIT);

            if (*is_last)
            {
                atomic_set_bit(downlink->flags, DOWNLINK_FLAG_COMPLETE);
                break;
            }
        }
    }

    *dst_len = total_bytes_copied;
    return 0;
}

bool pouch_gateway_downlink_is_complete(const struct pouch_gateway_downlink_context *downlink)
{
    return atomic_test_bit(downlink->flags, DOWNLINK_FLAG_COMPLETE);
}

void pouch_gateway_downlink_close(struct pouch_gateway_downlink_context *downlink)
{
    flush_block_queue(&downlink->block_queue);

    if (NULL != downlink->current_block)
    {
        block_free(downlink->current_block);
    }

    free(downlink);
}

void pouch_gateway_downlink_abort(struct pouch_gateway_downlink_context *downlink)
{
    LOG_INF("Aborting downlink");

    /* Downlink will be aborted after the current in flight CoAP
       block request is completed. */

    atomic_set_bit(downlink->flags, DOWNLINK_FLAG_TRANSPORT_ABORTED);

    /* If there are no more blocks, then just cleanup */

    if (pouch_gateway_downlink_is_complete(downlink))
    {
        pouch_gateway_downlink_close(downlink);
    }
}

void pouch_gateway_downlink_module_init(struct golioth_client *client)
{
    _client = client;
}
