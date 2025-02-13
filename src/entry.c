/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#include "entry.h"
#include "block.h"
#include "uplink.h"

#include <string.h>
#include <stdio.h>

#include <zephyr/sys/byteorder.h>

#define ENTRY_HEADER_OVERHEAD 5

struct pouch_entry
{
    const char *path;
    uint16_t content_type;
    size_t data_len;
    const void *data;
};

static struct pouch_buf *block;
static K_MUTEX_DEFINE(mut);

/* Entry format:
 *
 * Multi byte fields are big-endian.
 *
 *           |   0   |   1    |   2    |   3    |
 *           +------------------------------------+
 *         0 |     data_len   |   content_type    |
 *           +------------------------------------+
 *         4 | p_len | path      ...              |
 *           +------------------------------------+
 * 5 + p_len | data              ...              |
 *           +------------------------------------+
 */

static int write_header(struct pouch_buf *block, const struct pouch_entry *entry)
{
    uint8_t *buf = buf_next(block);
    size_t pathlen = strlen(entry->path);
    if (block_space_get(block) < ENTRY_HEADER_OVERHEAD + pathlen)
    {
        return -ENOMEM;
    }

    sys_put_be16(entry->data_len, &buf[0]);
    sys_put_be16(entry->content_type, &buf[2]);
    buf[4] = pathlen;
    memcpy(&buf[5], entry->path, pathlen);

    block->bytes += ENTRY_HEADER_OVERHEAD + pathlen;

    return 0;
}

static int write_entry(struct pouch_buf *block, const struct pouch_entry *entry)
{
    size_t original_offset = block->bytes;
    int err = write_header(block, entry);
    if (err)
    {
        block->bytes = original_offset;
        return err;
    }

    size_t len = block_write(block, entry->data, entry->data_len);
    if (len != entry->data_len)
    {
        block->bytes = original_offset;
        return err;
    }

    return 0;
}

int pouch_uplink_entry_write(const char *path,
                             uint16_t content_type,
                             const void *data,
                             size_t len,
                             k_timeout_t timeout)
{
    if (path == NULL || data == NULL || len == 0)
    {
        return -EINVAL;
    }

    int err = k_mutex_lock(&mut, timeout);
    if (err)
    {
        return err;
    }

    if (block == NULL)
    {
        block = block_alloc();
        if (block == NULL)
        {
            err = -ENOMEM;
            goto end;
        }
    }

    const struct pouch_entry entry = {
        .path = path,
        .content_type = content_type,
        .data_len = len,
        .data = data,
    };

    err = write_entry(block, &entry);
    if (err && block->bytes > 0)
    {
        // block is full
        block_finish(block);
        uplink_enqueue(block);

        // try again with a new block:
        block = block_alloc();
        if (block == NULL)
        {
            err = -ENOMEM;
            goto end;
        }

        err = write_entry(block, &entry);
    }

end:
    k_mutex_unlock(&mut);
    return err;
}

int entry_block_close(k_timeout_t timeout)
{
    int err = k_mutex_lock(&mut, timeout);
    if (err)
    {
        return err;
    }

    if (block)
    {
        block_finish(block);
        uplink_enqueue(block);
        block = NULL;
    }

    k_mutex_unlock(&mut);
    return 0;
}
