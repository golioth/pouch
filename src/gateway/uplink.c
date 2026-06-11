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

#include "../buf.h"

POUCH_LOG_REGISTER(gw_uplink, CONFIG_POUCH_GATEWAY_LOG_LEVEL);

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
    pouch_gateway_uplink_end_cb end_cb;
    void *end_cb_arg;
};

static void cleanup_uplink(struct pouch_gateway_uplink *uplink)
{
    struct pouch_buf *block;

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
 * Concatenate all queued blocks into a single contiguous buffer.
 * Caller frees @p out_buf.
 */
static int concat_blocks(struct pouch_gateway_uplink *uplink, uint8_t **out_buf, size_t *out_len)
{
    size_t total = uplink->total_len;

    if (total == 0)
    {
        *out_buf = NULL;
        *out_len = 0;
        return 0;
    }

    uint8_t *buf = malloc(total);
    if (buf == NULL)
    {
        POUCH_LOG_ERR("Failed to allocate %zu byte uplink buffer", total);
        return -ENOMEM;
    }

    size_t offset = 0;
    struct pouch_buf *block;

    while ((block = buf_queue_get(&uplink->queue)) != NULL)
    {
        size_t len = buf_size_get(block);

        struct pouch_bufview v;
        pouch_bufview_init(&v, block);
        pouch_bufview_memcpy(&v, buf + offset, len);

        offset += len;
        blockbuf_free(block);
    }

    *out_buf = buf;
    *out_len = offset;
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
 * Send all buffered uplink data via the registered cloud transport
 * and process the downlink response. Called from
 * pouch_gateway_uplink_close() after all data from the BLE
 * peripheral has been received.
 */
static void send_uplink_via_cloud(struct pouch_gateway_uplink *uplink)
{
    uint8_t *buf = NULL;
    size_t len = 0;
    int err;

    err = concat_blocks(uplink, &buf, &len);
    if (err)
    {
        POUCH_LOG_ERR("Failed to concatenate uplink blocks: %d", err);
        uplink->end_cb(uplink->end_cb_arg, POUCH_GATEWAY_UPLINK_ERROR_LOCAL);
        cleanup_uplink(uplink);
        return;
    }

    if (len == 0)
    {
        POUCH_LOG_DBG("No uplink data to send");
        uplink->end_cb(uplink->end_cb_arg, POUCH_GATEWAY_UPLINK_SUCCESS);
        cleanup_uplink(uplink);
        return;
    }

    if (!IS_ENABLED(CONFIG_POUCH_GATEWAY_CLOUD))
    {
        POUCH_LOG_INF("Cloud disabled, discarding %zu bytes of peripheral data", len);
        free(buf);
        uplink->end_cb(uplink->end_cb_arg, POUCH_GATEWAY_UPLINK_SUCCESS);
        cleanup_uplink(uplink);
        return;
    }

    POUCH_LOG_INF("Forwarding %zu bytes of peripheral data to cloud", len);

    err = pouch_gateway_cloud_forward_pouch(buf,
                                            len,
                                            uplink->downlink ? cloud_downlink_cb : NULL,
                                            uplink->downlink);
    free(buf);

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
                               size_t len,
                               bool is_last)
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
