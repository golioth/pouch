/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <pouch/blockbuf.h>
#include <pouch/gateway/cloud.h>
#include "downlink.h"
#include <pouch/gateway/uplink.h>
#include <pouch/port.h>

#include "uplink.h"
#include "../buf.h"

POUCH_LOG_REGISTER(gw_uplink, CONFIG_POUCH_GATEWAY_LOG_LEVEL);

POUCH_THREAD_STACK_DEFINE(gateway_work_q_stack, CONFIG_POUCH_GATEWAY_WORKQ_STACK_SIZE);
static pouch_work_q_t gateway_work_q;

enum pouch_flags
{
    POUCH_UPLINK_CLOSED,
};

struct pouch_gateway_uplink
{
    struct pouch_gateway_downlink_context *downlink;
    pouch_atomic_t flags[1];
    struct pouch_buf *wblock;
    pouch_buf_queue_t queue;
    size_t total_len;
    size_t remaining_len;
    struct pouch_buf *drain_block;
    struct pouch_bufview drain_view;
    pouch_gateway_uplink_end_cb end_cb;
    void *end_cb_arg;
};

static void cleanup_uplink(struct pouch_gateway_uplink *uplink)
{
    struct pouch_buf *block;

    if (uplink->drain_block != NULL)
    {
        blockbuf_free(uplink->drain_block);
    }

    while ((block = buf_queue_get(&uplink->queue)) != NULL)
    {
        blockbuf_free(block);
    }

    if (uplink->wblock != NULL)
    {
        blockbuf_free(uplink->wblock);
    }

    free(uplink);
}

static void submit_wblock(struct pouch_gateway_uplink *uplink)
{
    POUCH_LOG_DBG("Submitting block of size %zu", buf_size_get(uplink->wblock));
    uplink->total_len += buf_size_get(uplink->wblock);
    buf_queue_submit(&uplink->queue, uplink->wblock);
    uplink->wblock = NULL;
}

/*
 * Chunk callback handed to the cloud transport. Streams bytes out of
 * the queued blocks one CoAP-block at a time; blocks are freed as
 * soon as they are drained so the pool empties in step with the
 * uplink.
 */
static int uplink_chunk_cb(uint8_t *buf,
                           size_t buf_size,
                           size_t *chunk_len,
                           bool *is_last,
                           void *arg)
{
    struct pouch_gateway_uplink *uplink = arg;
    size_t written = 0;

    while (written < buf_size && uplink->remaining_len > 0)
    {
        if (uplink->drain_block == NULL)
        {
            uplink->drain_block = buf_queue_get(&uplink->queue);
            if (uplink->drain_block == NULL)
            {
                POUCH_LOG_ERR("Uplink drain ran out of blocks with %zu bytes still expected",
                              uplink->remaining_len);
                return -EIO;
            }
            pouch_bufview_init(&uplink->drain_view, uplink->drain_block);
        }

        size_t available = pouch_bufview_available(&uplink->drain_view);
        size_t take = MIN(available, buf_size - written);

        pouch_bufview_memcpy(&uplink->drain_view, buf + written, take);
        written += take;
        uplink->remaining_len -= take;

        if (pouch_bufview_available(&uplink->drain_view) == 0)
        {
            blockbuf_free(uplink->drain_block);
            uplink->drain_block = NULL;
        }
    }

    *chunk_len = written;
    *is_last = (uplink->remaining_len == 0);
    return 0;
}

/*
 * Cloud Block2 response callback adapter. Forwards data to the
 * gateway downlink module.
 */
static int cloud_downlink_cb(const uint8_t *data, size_t len, bool is_last, void *arg)
{
    struct pouch_gateway_downlink_context *downlink = arg;

    if (downlink == NULL)
    {
        return 0;
    }

    return pouch_gateway_downlink_block_cb(data, len, is_last, downlink);
}

/*
 * Stream all buffered uplink data through the registered cloud
 * transport and process the downlink response. Called from
 * pouch_gateway_uplink_close() after all data from the BLE
 * peripheral has been received.
 */
static void send_uplink_via_cloud(struct pouch_gateway_uplink *uplink)
{
    int err;

    if (uplink->total_len == 0)
    {
        POUCH_LOG_DBG("No uplink data to send");
        uplink->end_cb(uplink->end_cb_arg, POUCH_GATEWAY_UPLINK_SUCCESS);
        cleanup_uplink(uplink);
        return;
    }

    if (!IS_ENABLED(CONFIG_POUCH_GATEWAY_CLOUD))
    {
        POUCH_LOG_INF("Cloud disabled, discarding %zu bytes of peripheral data", uplink->total_len);
        uplink->end_cb(uplink->end_cb_arg, POUCH_GATEWAY_UPLINK_SUCCESS);
        cleanup_uplink(uplink);
        return;
    }

    POUCH_LOG_INF("Forwarding %zu bytes of peripheral data to cloud", uplink->total_len);

    uplink->remaining_len = uplink->total_len;

    err = pouch_gateway_cloud_forward_pouch(uplink_chunk_cb,
                                            uplink,
                                            uplink->downlink ? cloud_downlink_cb : NULL,
                                            uplink->downlink);
    if (err)
    {
        POUCH_LOG_ERR("Cloud forward failed: %d", err);
        if (uplink->downlink)
        {
            pouch_gateway_downlink_end_cb(err, uplink->downlink);
        }
        uplink->end_cb(uplink->end_cb_arg, POUCH_GATEWAY_UPLINK_ERROR_CLOUD);
    }
    else
    {
        if (uplink->downlink)
        {
            pouch_gateway_downlink_end_cb(0, uplink->downlink);
        }
        uplink->end_cb(uplink->end_cb_arg, POUCH_GATEWAY_UPLINK_SUCCESS);
    }

    cleanup_uplink(uplink);
}

/*
 * Max bytes per gateway block.  Each pouch_buf slot holds at least
 * CONFIG_POUCH_BLOCK_SIZE bytes; we conservatively use that as the
 * per-block capacity.
 */
#define GW_BLOCK_MAX_BYTES CONFIG_POUCH_BLOCK_SIZE

int pouch_gateway_uplink_write(struct pouch_gateway_uplink *uplink,
                               const uint8_t *payload,
                               size_t len)
{
    while (len)
    {
        if (uplink->wblock != NULL && buf_size_get(uplink->wblock) >= GW_BLOCK_MAX_BYTES)
        {
            submit_wblock(uplink);
        }

        if (uplink->wblock == NULL)
        {
            uplink->wblock = blockbuf_alloc(POUCH_NO_WAIT);
            if (uplink->wblock == NULL)
            {
                POUCH_LOG_ERR("Failed to alloc new block");
                return -ENOMEM;
            }
        }

        size_t space = GW_BLOCK_MAX_BYTES - buf_size_get(uplink->wblock);
        size_t bytes_to_copy = MIN(len, space);

        buf_write(uplink->wblock, payload, bytes_to_copy);

        len -= bytes_to_copy;
        payload += bytes_to_copy;
    }

    return 0;
}

void pouch_gateway_uplink_module_init(void)
{
    /* Cloud transport state is registered separately via
     * pouch_gateway_cloud_transport_register().
     */
    pouch_work_queue_init(&gateway_work_q);

    pouch_work_queue_start(&gateway_work_q,
                           gateway_work_q_stack,
                           CONFIG_POUCH_GATEWAY_WORKQ_STACK_SIZE,
                           CONFIG_POUCH_GATEWAY_WORKQ_PRIORITY,
                           "gw_workq");
}

void pouch_gateway_submit_close_work(pouch_work_t *work)
{
    pouch_work_submit_to_queue(&gateway_work_q, work);
}

struct pouch_gateway_uplink *pouch_gateway_uplink_open(
    struct pouch_gateway_downlink_context *downlink,
    pouch_gateway_uplink_end_cb end_cb,
    void *end_cb_arg)
{
    struct pouch_gateway_uplink *uplink = malloc(sizeof(struct pouch_gateway_uplink));
    if (uplink == NULL)
    {
        return NULL;
    }

    uplink->wblock = blockbuf_alloc(POUCH_NO_WAIT);
    if (uplink->wblock == NULL)
    {
        free(uplink);
        return NULL;
    }

    uplink->downlink = downlink;
    uplink->total_len = 0;
    uplink->remaining_len = 0;
    uplink->drain_block = NULL;
    pouch_atomic_set(uplink->flags, 0);
    buf_queue_init(&uplink->queue);
    uplink->end_cb = end_cb;
    uplink->end_cb_arg = end_cb_arg;

    return uplink;
}

void pouch_gateway_uplink_close(struct pouch_gateway_uplink *uplink)
{
    bool closed = pouch_atomic_test_and_set_bit(uplink->flags, POUCH_UPLINK_CLOSED);

    if (!closed && uplink->wblock != NULL && buf_size_get(uplink->wblock) > 0)
    {
        submit_wblock(uplink);
    }

    send_uplink_via_cloud(uplink);
}
