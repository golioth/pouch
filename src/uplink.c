/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#include "pouch.h"
#include "header.h"
#include "block.h"
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
    atomic_t flags;
    int error;

    struct
    {
        /** Blocks that are ready for processing */
        sys_slist_t queue;
        struct k_work work;
    } processing;
    struct
    {
        /** Blocks that are ready for transport */
        sys_slist_t queue;
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
    while (session_is_active() && pouch_is_open() && !sys_slist_is_empty(&uplink.processing.queue))
    {
        sys_snode_t *head = sys_slist_get(&uplink.processing.queue);
        struct pouch_buf *encrypted = crypto_encrypt_block(POUCH_BUF_FROM_SNODE(head));
        if (encrypted)
        {
            sys_slist_append(&uplink.transport.queue, &encrypted->node);
        }
    }

    if (pouch_is_closing())
    {
        atomic_set_bit(&uplink.flags, POUCH_CLOSED);
    }
}

void uplink_enqueue(struct pouch_buf *block)
{
    sys_slist_append(&uplink.processing.queue, &block->node);
    k_work_submit(&uplink.processing.work);
}

int pouch_uplink_close(k_timeout_t timeout)
{
    if (atomic_test_and_set_bit(&uplink.flags, POUCH_CLOSING))
    {
        return -EALREADY;
    }

    return entry_block_close(timeout);
}

int uplink_init(void)
{
    k_work_init(&uplink.processing.work, process_blocks);

    return 0;
}

// Transport API:

struct pouch_uplink *pouch_uplink_start(void)
{
    if (session_is_active())
    {
        return NULL;
    }

    crypto_pouch_start();

    struct pouch_buf *header = pouch_header_create();
    if (!header)
    {
        return NULL;
    }

    sys_slist_append(&uplink.transport.queue, &header->node);

    atomic_set_bit(&uplink.flags, SESSION_ACTIVE);

    pouch_event_emit(POUCH_EVENT_SESSION_START);

    // Process any pending blocks:
    if (!sys_slist_is_empty(&uplink.processing.queue))
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
        sys_snode_t *n = sys_slist_peek_head(&uplink->transport.queue);
        if (!n)
        {
            break;
        }

        struct pouch_buf *block = POUCH_BUF_FROM_SNODE(n);

        size_t bytes = buf_read(block, &dst[*len], maxlen - *len, uplink->transport.offset);
        uplink->transport.offset += bytes;
        *len += bytes;

        if (uplink->transport.offset == block->bytes)
        {
            sys_slist_get(&uplink->transport.queue);
            uplink->transport.offset = 0;
            block_free(block);
        }
    }

    if (pouch_is_open() || !sys_slist_is_empty(&uplink->transport.queue))
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
    sys_snode_t *n;
    while ((n = sys_slist_get(&uplink->transport.queue)))
    {
        block_free(POUCH_BUF_FROM_SNODE(n));
    }

    atomic_clear(&uplink->flags);
}
