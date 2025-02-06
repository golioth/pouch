/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#include "buf.h"
#include <stdlib.h>
#include <string.h>

static K_SEM_DEFINE(mem_sem, 0, 1);
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

struct pouch_buf *buf_alloc(size_t size, k_timeout_t timeout)
{
    k_timepoint_t end = sys_timepoint_calc(timeout);
    struct pouch_buf *buf;

    while (!(buf = malloc(sizeof(struct pouch_buf) + size)))
    {
        /* As the system heap doesn't have a blocking allocation function,
         * we have to run our own based on our own freeing.
         */
        if (atomic_get(&bufs) == 0)
        {
            // If we fail malloc while no buffers are in use, we're truly out of memory.
            return NULL;
        }

        k_sem_take(&mem_sem, sys_timepoint_timeout(end));
    }

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
    k_sem_give(&mem_sem);
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
