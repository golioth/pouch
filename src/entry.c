/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#include "entry.h"
#include "block.h"
#include "uplink.h"

#include <string.h>
#include <stdio.h>

#include <zephyr/sys/byteorder.h>
#include <pouch/downlink.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(entry);

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

struct pouch_downlink_entry
{
    union
    {
        uint8_t header[5];
        struct
        {
            uint16_t data_len;
            uint16_t content_type;
            uint8_t path_len;
        } __packed;
    };

    /* Internal */
    size_t header_len;
    size_t path_and_data_len;
    size_t path_and_data_consumed;

    uint8_t path[256];
    const uint8_t *path_and_data;
};

static struct pouch_downlink_entry pouch_downlink_entry;

static const char *entry_content_format_str(int content_format)
{
    switch (content_format)
    {
        case POUCH_CONTENT_TYPE_OCTET_STREAM:
            return "octet-stream";
        case POUCH_CONTENT_TYPE_JSON:
            return "json";
        case POUCH_CONTENT_TYPE_CBOR:
            return "cbor";
    }

    return "unsupported";
}

static void entry_finish(struct pouch_downlink_entry *entry, bool is_last)
{
    const uint8_t *data;
    size_t len;
    size_t offset;

    if (entry->path_and_data_consumed == 0)
    {
        data = &entry->path_and_data[entry->path_len];
        len = entry->path_and_data_len - entry->path_len;
        offset = 0;

        /* Copy path for future use */
        memcpy(entry->path, entry->path_and_data, entry->path_len);
        entry->path[entry->path_len] = '\0';
    }
    else
    {
        data = entry->path_and_data;
        len = entry->path_and_data_len - entry->path_and_data_consumed;
        offset = entry->path_and_data_consumed - entry->path_len;
    }

    downlink_received(entry->path, entry->content_type, data, len, offset, is_last);

    if (is_last)
    {
        entry->header_len = 0;
        entry->path_and_data_len = 0;
        entry->path_and_data_consumed = 0;
    }
    else
    {
        entry->path_and_data_consumed = entry->path_and_data_len;
    }
}

static size_t entry_push_one(struct pouch_downlink_entry *entry,
                             const uint8_t *buf,
                             size_t buf_len,
                             bool is_stream)
{
    const uint8_t *buf_start = buf;

    /* Header */
    while (buf_len)
    {
        if (entry->header_len >= sizeof(entry->header))
        {
            break;
        }

        entry->header[entry->header_len] = *buf;
        entry->header_len++;

        buf++;
        buf_len--;

        if (entry->header_len == sizeof(entry->header))
        {
            /* Header complete */
            LOG_HEXDUMP_DBG(entry->header, sizeof(entry->header), "entry header raw");

            entry->data_len = sys_be16_to_cpu(entry->data_len);
            entry->content_type = sys_be16_to_cpu(entry->content_type);

            LOG_DBG("data_len  %d", entry->data_len);
            LOG_DBG("content_type %s (%d)",
                    entry_content_format_str(entry->content_type),
                    (int) entry->content_type);
            LOG_DBG("path_len  %d", entry->path_len);
        }
    }

    /* Data */
    while (buf_len)
    {
        size_t to_consume = buf_len;

        if (!is_stream)
        {
            size_t total_length = entry->data_len + entry->path_len;
            size_t remaining_length = total_length - entry->path_and_data_len;

            to_consume = MIN(remaining_length, buf_len);
        }

        LOG_DBG("entry path_and_data chunk [%2d : %2d]",
                (int) entry->path_and_data_len,
                (int) entry->path_and_data_len + to_consume);
        LOG_HEXDUMP_DBG(buf, to_consume, "entry path_and_data chunk");

        entry->path_and_data = buf;
        entry->path_and_data_len += to_consume;

        buf += to_consume;
        buf_len -= to_consume;

        if (!is_stream && entry->path_and_data_len >= entry->data_len + entry->path_len)
        {
            entry_finish(entry, true);
            break;
        }
    }

    return buf - buf_start;
}

static size_t entry_push(struct pouch_downlink_entry *entry,
                         const uint8_t *buf,
                         size_t buf_len,
                         bool is_stream)
{
    const uint8_t *buf_start = buf;
    size_t consumed;

    while (buf_len)
    {
        consumed = entry_push_one(entry, buf, buf_len, is_stream);

        buf += consumed;
        buf_len -= consumed;
    }

    return buf - buf_start;
}

void pouch_downlink_entries_push(const uint8_t *buf, size_t buf_len, bool is_stream, bool is_last)
{
    struct pouch_downlink_entry *entry = &pouch_downlink_entry;

    if (is_stream && entry->path_and_data_consumed == 0)
    {
        /* Header does not contain data_len in this case */
        entry->header_len = sizeof(entry->data_len);
    }

    entry_push(entry, buf, buf_len, is_stream);

    if (is_stream)
    {
        entry_finish(entry, is_last);
    }
}

static int write_entry(struct pouch_buf *block, const struct pouch_entry *entry)
{
    size_t pathlen = strlen(entry->path);
    if (block_space_get(block) < ENTRY_HEADER_OVERHEAD + pathlen + entry->data_len)
    {
        return -ENOMEM;
    }

    sys_put_be16(entry->data_len, buf_claim(block, sizeof(uint16_t)));
    sys_put_be16(entry->content_type, buf_claim(block, sizeof(uint16_t)));
    *buf_claim(block, 1) = pathlen;
    buf_write(block, entry->path, pathlen);
    buf_write(block, entry->data, entry->data_len);

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

    bool block_is_new = false;
    int err = k_mutex_lock(&mut, timeout);
    if (err)
    {
        return err;
    }

    if (block == NULL)
    {
        block_is_new = true;
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
    if (err && !block_is_new)
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

    if (block && block_size_get(block) > 0)
    {
        block_finish(block);
        uplink_enqueue(block);
        block = NULL;
    }

    k_mutex_unlock(&mut);
    return 0;
}
