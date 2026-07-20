/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "buf.h"
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <pouch/port.h>

static pouch_atomic_t bufs;

struct pouch_buf
{
    pouch_slist_node_t node;
    /** Number of bytes in the buffer */
    size_t bytes;
    /** Data */
    uint8_t buf[];
};

POUCH_STATIC_ASSERT(sizeof(struct pouch_buf) == POUCH_BUF_OVERHEAD,
                    "Invalid overhead in pouch buf");

void buf_write(struct pouch_buf *buf, const uint8_t *data, size_t len)
{
    memcpy(buf_claim(buf, len), data, len);
}

size_t buf_bytes_get(const struct pouch_buf *buf)
{
    return buf->bytes;
}

void buf_bytes_set(struct pouch_buf *buf, size_t offset)
{
    buf->bytes = offset;
}

uint8_t *buf_next(struct pouch_buf *buf)
{
    return &buf->buf[buf->bytes];
}

uint8_t *buf_claim(struct pouch_buf *buf, size_t bytes)
{
    uint8_t *data = &buf->buf[buf->bytes];
    buf->bytes += bytes;
    return data;
}

pouch_buf_state_t buf_state_get(const struct pouch_buf *buf)
{
    return buf->bytes;
}

void buf_restore(struct pouch_buf *buf, pouch_buf_state_t state)
{
    buf->bytes = state;
}

size_t buf_size_get(const struct pouch_buf *buf)
{
    return buf->bytes;
}

void buf_init(struct pouch_buf *buf)
{
    buf->bytes = POUCH_BUF_STATE_INITIAL;
    pouch_slist_node_init(&buf->node);
}

struct pouch_buf *buf_alloc(size_t size)
{
    struct pouch_buf *buf = malloc(sizeof(struct pouch_buf) + size);
    if (buf != NULL)
    {
        pouch_atomic_inc(&bufs);
        buf_init(buf);
    }

    return buf;
}

void buf_free(struct pouch_buf *buf)
{
    free(buf);
    if (buf)
    {
        pouch_atomic_dec(&bufs);
    }
}

int buf_active_count(void)
{
    return pouch_atomic_get_value(&bufs);
}

size_t buf_trim_start(struct pouch_buf *buf, size_t bytes)
{
    bytes = MIN(bytes, buf->bytes);
    buf->bytes -= bytes;
    memmove(&buf->buf[0], &buf->buf[bytes], buf->bytes);
    return bytes;
}

size_t buf_trim_end(struct pouch_buf *buf, size_t bytes)
{
    bytes = MIN(bytes, buf->bytes);
    buf->bytes -= bytes;
    return bytes;
}

void buf_queue_init(pouch_buf_queue_t *queue)
{
    pouch_slist_init(&queue->slist);
    pouch_mutex_init(&queue->lock);
}

void buf_queue_submit(pouch_buf_queue_t *queue, struct pouch_buf *buf)
{
    pouch_mutex_lock(&queue->lock, POUCH_FOREVER);
    pouch_slist_append(&queue->slist, &buf->node);
    pouch_mutex_unlock(&queue->lock);
}

struct pouch_buf *buf_queue_get(pouch_buf_queue_t *queue)
{
    pouch_mutex_lock(&queue->lock, POUCH_FOREVER);
    pouch_slist_node_t *n = pouch_slist_get(&queue->slist);
    pouch_mutex_unlock(&queue->lock);

    return n ? CONTAINER_OF(n, struct pouch_buf, node) : NULL;
}

struct pouch_buf *buf_queue_peek(pouch_buf_queue_t *queue)
{
    pouch_mutex_lock(&queue->lock, POUCH_FOREVER);
    pouch_slist_node_t *n = pouch_slist_peek_head(&queue->slist);
    pouch_mutex_unlock(&queue->lock);

    return n ? CONTAINER_OF(n, struct pouch_buf, node) : NULL;
}

static const uint8_t *bufview_read(struct pouch_bufview *v, size_t bytes)
{
    const uint8_t *data = &v->buf->buf[v->offset];
    v->offset += bytes;

    return data;
}

size_t pouch_bufview_memcpy(struct pouch_bufview *v, void *dst, size_t bytes)
{
    bytes = MIN(bytes, pouch_bufview_available(v));
    memcpy(dst, bufview_read(v, bytes), bytes);
    return bytes;
}

const void *pouch_bufview_read(struct pouch_bufview *v, size_t bytes)
{
    if (pouch_bufview_available(v) < bytes)
    {
        return NULL;
    }

    return bufview_read(v, bytes);
}

int pouch_bufview_read_byte(struct pouch_bufview *v, uint8_t *dst)
{
    if (pouch_bufview_available(v) < sizeof(uint8_t))
    {
        return -ENODATA;
    }
    *dst = *bufview_read(v, sizeof(uint8_t));
    return 0;
}

int pouch_bufview_read_be16(struct pouch_bufview *v, uint16_t *dst)
{
    if (pouch_bufview_available(v) < sizeof(uint16_t))
    {
        return -ENODATA;
    }
    *dst = pouch_get_be16(bufview_read(v, sizeof(uint16_t)));
    return 0;
}

int pouch_bufview_read_be32(struct pouch_bufview *v, uint32_t *dst)
{
    if (pouch_bufview_available(v) < sizeof(uint32_t))
    {
        return -ENODATA;
    }
    *dst = pouch_get_be32(bufview_read(v, sizeof(uint32_t)));
    return 0;
}

int pouch_bufview_read_be64(struct pouch_bufview *v, uint64_t *dst)
{
    if (pouch_bufview_available(v) < sizeof(uint64_t))
    {
        return -ENODATA;
    }
    *dst = pouch_get_be64(bufview_read(v, sizeof(uint64_t)));
    return 0;
}

size_t pouch_bufview_available(const struct pouch_bufview *v)
{
    if (v->buf == NULL)
    {
        return 0;
    }

    return v->buf->bytes - v->offset;
}
