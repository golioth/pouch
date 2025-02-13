/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#include "buf.h"
#include <stdlib.h>
#include <string.h>

static atomic_t bufs;

void buf_write(struct pouch_buf *buf, const uint8_t *data, size_t len)
{
    memcpy(&buf->buf[buf->bytes], data, len);
    buf->bytes += len;
}

uint8_t *buf_next(struct pouch_buf *buf)
{
    return &buf->buf[buf->bytes];
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
    atomic_dec(&bufs);
}

size_t buf_read(struct pouch_buf *buf, uint8_t *data, size_t len, size_t offset)
{
    size_t bytes_to_copy = MIN(len, buf->bytes - offset);
    memcpy(data, &buf->buf[offset], bytes_to_copy);

    return bytes_to_copy;
}

int buf_active_count(void)
{
    return atomic_get(&bufs);
}
