/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#include "pouch.h"
#include "header.h"
#include "entry.h"
#include "crypto.h"

#include <pouch/uplink.h>
#include <pouch/events.h>
#include <pouch/transport/uplink.h>

#include <stdlib.h>
#include <zephyr/init.h>
#include <zephyr/sys/iterable_sections.h>
#include <zephyr/sys/ring_buffer.h>

enum flags
{
    SESSION_ACTIVE,
    POUCH_CLOSING,
    POUCH_CLOSED,
};

struct pouch_uplink
{
    struct pouch_config config;
    struct pouch_buf *header;
    atomic_t flags;
    atomic_t id;
    int error;

    struct
    {
        /** Blocks that are ready for processing */
        pouch_buf_queue_t queue;
        struct k_work work;
    } processing;
    struct
    {
        /** Blocks that are ready for transport */
        pouch_buf_queue_t queue;
        /** Read offset in bytes */
        size_t offset;
    } transport;
};

/** Single uplink session */
static struct pouch_uplink uplink;

static bool session_is_active(void)
{
    return atomic_get(&uplink.flags) & BIT(SESSION_ACTIVE);
}

static bool pouch_is_open(void)
{
    return !(atomic_get(&uplink.flags) & BIT(POUCH_CLOSED));
}

static bool pouch_is_closing(void)
{
    return (atomic_get(&uplink.flags) & BIT(POUCH_CLOSING));
}

static void process_blocks(struct k_work *work)
{
    while (session_is_active() && pouch_is_open() && !buf_queue_is_empty(&uplink.processing.queue))
    {
        struct pouch_buf *encrypted = crypto_encrypt_block(buf_queue_get(&uplink.processing.queue));
        if (!encrypted)
        {
            continue;
        }

        if (uplink.header)
        {
            buf_queue_submit(&uplink.transport.queue, uplink.header);
            uplink.header = NULL;
        }

        buf_queue_submit(&uplink.transport.queue, encrypted);
    }

    if (pouch_is_closing())
    {
        atomic_set_bit(&uplink.flags, POUCH_CLOSED);
    }
}

void uplink_enqueue(struct pouch_buf *block)
{
    buf_queue_submit(&uplink.processing.queue, block);
    k_work_submit(&uplink.processing.work);
}

int pouch_uplink_close(k_timeout_t timeout)
{
    if (atomic_test_and_set_bit(&uplink.flags, POUCH_CLOSING))
    {
        return -EALREADY;
    }

    int err = entry_block_close(timeout);

    k_work_submit(&uplink.processing.work);

    return err;
}

int uplink_init(const struct pouch_config *config)
{
    uplink.config = *config;
    buf_queue_init(&uplink.processing.queue);
    buf_queue_init(&uplink.transport.queue);
    k_work_init(&uplink.processing.work, process_blocks);

    return 0;
}

uint32_t uplink_session_id(void)
{
    return atomic_get(&uplink.id);
}

// Transport API:

struct pouch_uplink *pouch_uplink_start(void)
{
    if (atomic_test_and_set_bit(&uplink.flags, SESSION_ACTIVE))
    {
        return NULL;
    }

    crypto_pouch_start();

    // Create the header, but don't push it to the queue until we have data to send:
    uplink.header = pouch_header_create(&uplink.config);
    if (!uplink.header)
    {
        return NULL;
    }

    pouch_event_emit(POUCH_EVENT_SESSION_START);

    // Process any pending blocks:
    if (!buf_queue_is_empty(&uplink.processing.queue))
    {
        k_work_submit(&uplink.processing.work);
    }

    return &uplink;
}

enum pouch_result pouch_uplink_fill(struct pouch_uplink *uplink, uint8_t *dst, size_t *len)
{
    if (!session_is_active())
    {
        return POUCH_ERROR;
    }

    size_t maxlen = *len;
    *len = 0;

    while (*len < maxlen)
    {
        struct pouch_buf *block = buf_queue_peek(&uplink->transport.queue);
        if (!block)
        {
            break;
        }

        size_t bytes = buf_read(block, &dst[*len], maxlen - *len, uplink->transport.offset);
        uplink->transport.offset += bytes;
        *len += bytes;

        if (uplink->transport.offset == buf_size_get(block))
        {
            buf_queue_get(&uplink->transport.queue);
            uplink->transport.offset = 0;
            buf_free(block);
        }
    }

    if (pouch_is_open() || !buf_queue_is_empty(&uplink->transport.queue))
    {
        return POUCH_MORE_DATA;
    }

    atomic_clear_bit(&uplink->flags, SESSION_ACTIVE);
    pouch_event_emit(POUCH_EVENT_SESSION_END);

    return POUCH_NO_MORE_DATA;
}

int pouch_uplink_error(struct pouch_uplink *uplink)
{
    return uplink->error;
}

void pouch_uplink_finish(struct pouch_uplink *uplink)
{
    // Free any remaining blocks, as they won't be valid in the next pouch:
    struct pouch_buf *buf;
    while ((buf = buf_queue_get(&uplink->transport.queue)))
    {
        buf_free(buf);
    }

    if (uplink->header)
    {
        buf_free(uplink->header);
        uplink->header = NULL;
    }

    atomic_inc(&uplink->id);
    if (atomic_clear(&uplink->flags) & BIT(SESSION_ACTIVE))
    {
        /* The transport didn't pull down all the data, so
         * we didn't emit the end event, and need to do it
         * here instead.
         */
        pouch_event_emit(POUCH_EVENT_SESSION_END);
    }
}
