/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pouch.h"
#include "header.h"
#include "entry.h"
#include "stream.h"
#include "crypto.h"

#include <pouch/uplink.h>
#include <pouch/events.h>
#include <pouch/port.h>
#include <pouch/transport/uplink.h>

#include <stdlib.h>

enum flags
{
    SESSION_ACTIVE,
    POUCH_CLOSING,
    POUCH_CLOSED,
};

struct pouch_uplink
{
    struct pouch_buf *header;
    pouch_atomic_t flags[1];
    pouch_atomic_t id;
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
        /** Buffer reader for the buffer currently being processed. */
        struct pouch_bufview reader;
    } transport;
};

/** Single uplink session */
static struct pouch_uplink uplink;

static bool session_is_active(void)
{
    return pouch_atomic_test_bit(uplink.flags, SESSION_ACTIVE);
}

static bool pouch_is_open(void)
{
    return !pouch_atomic_test_bit(uplink.flags, POUCH_CLOSED);
}

static bool pouch_is_closing(void)
{
    return pouch_atomic_test_bit(uplink.flags, POUCH_CLOSING);
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

    if (pouch_is_closing() && !stream_is_open())
    {
        pouch_atomic_set_bit(uplink.flags, POUCH_CLOSED);
    }
}

static void end_session(void)
{
    crypto_session_end();
    pouch_atomic_clear_bit(uplink.flags, SESSION_ACTIVE);
    pouch_event_emit(POUCH_EVENT_SESSION_END);
}

void uplink_enqueue(struct pouch_buf *block)
{
    buf_queue_submit(&uplink.processing.queue, block);
    k_work_submit(&uplink.processing.work);
}

int pouch_uplink_close(k_timeout_t timeout)
{
    if (pouch_atomic_test_and_set_bit(uplink.flags, POUCH_CLOSING))
    {
        return -EALREADY;
    }

    int err = entry_block_close(timeout);

    k_work_submit(&uplink.processing.work);

    return err;
}

void uplink_init(void)
{
    buf_queue_init(&uplink.processing.queue);
    buf_queue_init(&uplink.transport.queue);
    k_work_init(&uplink.processing.work, process_blocks);
}

uint32_t uplink_session_id(void)
{
    return pouch_atomic_get(&uplink.id);
}

// emit uplink calls in event handler to ensure that they run in the pouch processing thread.
static void event_handler(enum pouch_event evt, void *ctx)
{
    if (evt != POUCH_EVENT_SESSION_START)
    {
        return;
    }

    POUCH_TYPE_SECTION_FOREACH(pouch_uplink_handler_t, pouch_uplink_handler, handler)
    {
        if (handler != NULL)
        {
            (*handler)();
        }
    }

    pouch_uplink_close(K_FOREVER);
}

POUCH_EVENT_HANDLER(event_handler, NULL);

// Transport API:

struct pouch_uplink *pouch_uplink_start(void)
{
    int err;

    if (pouch_atomic_test_and_set_bit(uplink.flags, SESSION_ACTIVE))
    {
        return NULL;
    }

    err = crypto_session_start();
    if (err)
    {
        pouch_atomic_clear_bit(uplink.flags, SESSION_ACTIVE);
        return NULL;
    }

    err = crypto_pouch_start();
    if (err)
    {
        pouch_atomic_clear_bit(uplink.flags, SESSION_ACTIVE);
        return NULL;
    }

    // Create the header, but don't push it to the queue until we have data to send:
    uplink.header = pouch_header_create();
    if (!uplink.header)
    {
        pouch_atomic_clear_bit(uplink.flags, SESSION_ACTIVE);
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
        if (!pouch_bufview_is_ready(&uplink->transport.reader))
        {
            struct pouch_buf *buf = buf_queue_get(&uplink->transport.queue);
            if (buf == NULL)
            {
                break;
            }

            pouch_bufview_init(&uplink->transport.reader, buf);
        }

        *len += pouch_bufview_memcpy(&uplink->transport.reader, &dst[*len], maxlen - *len);

        if (!pouch_bufview_available(&uplink->transport.reader))
        {
            pouch_bufview_free(&uplink->transport.reader);
        }
    }

    if (pouch_is_open() || pouch_bufview_available(&uplink->transport.reader)
        || !buf_queue_is_empty(&uplink->transport.queue))
    {
        return POUCH_MORE_DATA;
    }

    end_session();

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

    pouch_bufview_free(&uplink->transport.reader);

    if (uplink->header)
    {
        buf_free(uplink->header);
        uplink->header = NULL;
    }

    pouch_atomic_inc(&uplink->id);
    if (pouch_atomic_clear(uplink->flags) & BIT(SESSION_ACTIVE))
    {
        /* The transport didn't pull down all the data, so
         * we didn't emit the end event, and need to do it
         * here instead.
         */
        end_session();
    }
}
