/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>

#include <zephyr/sys/atomic_types.h>

#include <golioth/gateway.h>
#include <golioth/stream.h>

#include <pouch/gateway/downlink.h>
#include <pouch/gateway/uplink.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(uplink, CONFIG_POUCH_GATEWAY_LOG_LEVEL);

enum pouch_flags
{
    POUCH_UPLINK_CLOSED,
    POUCH_UPLINK_SENDING,
};

struct pouch_block
{
    sys_snode_t node;
    size_t len;
    uint8_t data[CONFIG_GOLIOTH_BLOCKWISE_UPLOAD_MAX_BLOCK_SIZE];
};

struct pouch_gateway_uplink
{
    struct gateway_uplink *session;
    uint32_t block_idx;
    atomic_t flags[1];
    struct pouch_block *wblock;
    struct pouch_block *rblock;
    sys_slist_t queue;
    pouch_gateway_uplink_end_cb end_cb;
    void *end_cb_arg;
};

static struct golioth_client *client;

static void process_uplink(struct pouch_gateway_uplink *uplink);

static void cleanup_uplink(struct pouch_gateway_uplink *uplink)
{
    if (IS_ENABLED(CONFIG_POUCH_GATEWAY_CLOUD))
    {
        golioth_gateway_uplink_finish(uplink->session);
    }

    sys_snode_t *n;
    while ((n = sys_slist_get(&uplink->queue)) != NULL)
    {
        free(CONTAINER_OF(n, struct pouch_block, node));
    }

    free(uplink->wblock);
    free(uplink->rblock);
    free(uplink);
}

static void block_upload_callback(struct golioth_client *client,
                                  enum golioth_status status,
                                  const struct golioth_coap_rsp_code *coap_rsp_code,
                                  const char *path,
                                  size_t block_size,
                                  void *arg)
{
    struct pouch_gateway_uplink *uplink = arg;

    if (!atomic_test_and_clear_bit(uplink->flags, POUCH_UPLINK_SENDING))
    {
        LOG_ERR("Not sending");
        return;
    }

    free(uplink->rblock);
    uplink->rblock = NULL;

    if (status != GOLIOTH_OK)
    {
        LOG_ERR("Failed to deliver block: %d", status);
        uplink->end_cb(uplink->end_cb_arg, POUCH_GATEWAY_UPLINK_ERROR_CLOUD);
        cleanup_uplink(uplink);
        return;
    }

    process_uplink(uplink);
}

static void process_uplink(struct pouch_gateway_uplink *uplink)
{
    enum golioth_status status;
    if (atomic_test_and_set_bit(uplink->flags, POUCH_UPLINK_SENDING))
    {
        LOG_DBG("Already processing queue");
        return;
    }

    bool closed = atomic_test_bit(uplink->flags, POUCH_UPLINK_CLOSED);

    sys_snode_t *n = sys_slist_get(&uplink->queue);
    if (n == NULL)
    {
        LOG_DBG("No blocks to process");
        if (closed)
        {
            uplink->end_cb(uplink->end_cb_arg, POUCH_GATEWAY_UPLINK_SUCCESS);
            cleanup_uplink(uplink);
            return;
        }

        atomic_clear_bit(uplink->flags, POUCH_UPLINK_SENDING);
        return;
    }

    uplink->rblock = CONTAINER_OF(n, struct pouch_block, node);

    LOG_DBG("Processing block %zu of size %zu", uplink->block_idx, uplink->rblock->len);

    if (!IS_ENABLED(CONFIG_POUCH_GATEWAY_CLOUD))
    {
        free(uplink->rblock);
        uplink->rblock = NULL;
        atomic_clear_bit(uplink->flags, POUCH_UPLINK_SENDING);

        return;
    }

    if (uplink->rblock->len == 0)
    {
        LOG_WRN("Skipping zero length block");

        free(uplink->rblock);
        uplink->rblock = NULL;
        atomic_clear_bit(uplink->flags, POUCH_UPLINK_SENDING);

        return;
    }

    status = golioth_gateway_uplink_block(uplink->session,
                                          uplink->block_idx++,
                                          uplink->rblock->data,
                                          uplink->rblock->len,
                                          sys_slist_is_empty(&uplink->queue) && closed,
                                          block_upload_callback,
                                          uplink);
    if (status != GOLIOTH_OK)
    {
        LOG_ERR("Failed to deliver block: %d", status);
        uplink->end_cb(uplink->end_cb_arg, POUCH_GATEWAY_UPLINK_ERROR_LOCAL);
        cleanup_uplink(uplink);
    }
}

static struct pouch_block *block_alloc(struct pouch_gateway_uplink *uplink)
{
    struct pouch_block *block = malloc(sizeof(struct pouch_block));
    if (block == NULL)
    {
        LOG_ERR("Failed to alloc block");
        return NULL;
    }

    block->len = 0;

    return block;
}

static void submit_block(struct pouch_gateway_uplink *uplink)
{
    LOG_DBG("Submitting block of size %zu", uplink->wblock->len);
    sys_slist_append(&uplink->queue, &uplink->wblock->node);
    uplink->wblock = NULL;
}

int pouch_gateway_uplink_write(struct pouch_gateway_uplink *uplink,
                               const uint8_t *payload,
                               size_t len,
                               bool is_last)
{
    while (len)
    {
        if (uplink->wblock != NULL && uplink->wblock->len == sizeof(uplink->wblock->data))
        {
            submit_block(uplink);
        }

        if (uplink->wblock == NULL)
        {
            uplink->wblock = block_alloc(uplink);
            if (uplink->wblock == NULL)
            {
                LOG_ERR("Failed to alloc new block");
                return -ENOMEM;
            }
        }

        size_t bytes_to_copy = MIN(len, sizeof(uplink->wblock->data) - uplink->wblock->len);

        memcpy(&uplink->wblock->data[uplink->wblock->len], payload, bytes_to_copy);
        uplink->wblock->len += bytes_to_copy;

        len -= bytes_to_copy;
        payload += bytes_to_copy;
    }

    process_uplink(uplink);

    return 0;
}

void pouch_gateway_uplink_module_init(struct golioth_client *c)
{
    client = c;
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

    uplink->wblock = block_alloc(uplink);
    if (uplink->wblock == NULL)
    {
        free(uplink);
        return NULL;
    }

    if (IS_ENABLED(CONFIG_POUCH_GATEWAY_CLOUD))
    {
        uplink->session = golioth_gateway_uplink_start(client,
                                                       pouch_gateway_downlink_block_cb,
                                                       pouch_gateway_downlink_end_cb,
                                                       downlink);
        if (uplink->session == NULL)
        {
            LOG_ERR("Failed to start blockwise upload");
            free(uplink->wblock);
            free(uplink);
            return NULL;
        }
    }

    uplink->rblock = NULL;
    uplink->block_idx = 0;
    atomic_set(uplink->flags, 0);
    sys_slist_init(&uplink->queue);
    uplink->end_cb = end_cb;
    uplink->end_cb_arg = end_cb_arg;

    return uplink;
}

void pouch_gateway_uplink_close(struct pouch_gateway_uplink *uplink)
{
    bool closed = atomic_test_and_set_bit(uplink->flags, POUCH_UPLINK_CLOSED);

    if (!closed && uplink->wblock != NULL)
    {
        submit_block(uplink);
    }

    process_uplink(uplink);
}
