/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#include "buf.h"
#include <stdlib.h>
#include <string.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(pouch_buf, LOG_LEVEL_DBG);

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

    LOG_INF("Allocating %p. TOTAL: %d", buf, buf_active_count());

    return buf;
}

void buf_free(struct pouch_buf *buf)
{
    free(buf);
    atomic_dec(&bufs);
    LOG_INF("Freeing %p. TOTAL: %d", buf, buf_active_count());
}

size_t buf_read(const struct pouch_buf *buf, uint8_t *data, size_t len, size_t offset)
{
    size_t bytes_to_copy = MIN(len, buf->bytes - offset);
    memcpy(data, &buf->buf[offset], bytes_to_copy);

    return bytes_to_copy;
}

int buf_active_count(void)
{
    return atomic_get(&bufs);
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
