/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#include "buf.h"
#include <stdlib.h>
#include <string.h>
#include <zephyr/sys/byteorder.h>

static atomic_t bufs;

struct pouch_buf
{
    sys_snode_t node;
    /** Number of bytes in the buffer */
    size_t bytes;
    /** Data */
    uint8_t buf[];
};

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

struct pouch_buf *buf_alloc(size_t size)
{
    struct pouch_buf *buf = malloc(sizeof(struct pouch_buf) + size);
    if (buf != NULL)
    {
        atomic_inc(&bufs);
        buf->bytes = 0;
    }

    return buf;
}

void buf_free(struct pouch_buf *buf)
{
    free(buf);
    if (buf)
    {
        atomic_dec(&bufs);
    }
}

int buf_active_count(void)
{
    return atomic_get(&bufs);
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
    sys_slist_init(queue);
}

void buf_queue_submit(pouch_buf_queue_t *queue, struct pouch_buf *buf)
{
    sys_slist_append(queue, &buf->node);
}

struct pouch_buf *buf_queue_get(pouch_buf_queue_t *queue)
{
    sys_snode_t *n = sys_slist_get(queue);
    return n ? CONTAINER_OF(n, struct pouch_buf, node) : NULL;
}

struct pouch_buf *buf_queue_peek(pouch_buf_queue_t *queue)
{
    sys_snode_t *n = sys_slist_peek_head(queue);
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

uint8_t pouch_bufview_read_byte(struct pouch_bufview *v)
{
    return *bufview_read(v, 1);
}

uint16_t pouch_bufview_read_be16(struct pouch_bufview *v)
{
    return sys_get_be16(bufview_read(v, sizeof(uint16_t)));
}

uint32_t pouch_bufview_read_be32(struct pouch_bufview *v)
{
    return sys_get_be32(bufview_read(v, sizeof(uint32_t)));
}

uint64_t pouch_bufview_read_be64(struct pouch_bufview *v)
{
    return sys_get_be64(bufview_read(v, sizeof(uint64_t)));
}

size_t pouch_bufview_available(const struct pouch_bufview *v)
{
    if (v->buf == NULL)
    {
        return 0;
    }

    return v->buf->bytes - v->offset;
}
