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

static void downlink_start(unsigned int stream_id, const char *path, uint16_t content_type)
{
    LOG_DBG("Entry stream_id: %u", stream_id);
    LOG_DBG("Entry path: %s", path);
    LOG_DBG("Entry content_type: %u", content_type);

    STRUCT_SECTION_FOREACH(pouch_downlink_handler, handler)
    {
        handler->start_cb(stream_id, path, content_type);
    }
}

static void downlink_data(unsigned int stream_id, const void *data, size_t len, bool is_last)
{
    LOG_DBG("Entry stream_id: %u", stream_id);
    LOG_DBG("Entry is_last: %d", (int) is_last);
    LOG_HEXDUMP_DBG(data, len, "Entry data");

    STRUCT_SECTION_FOREACH(pouch_downlink_handler, handler)
    {
        handler->data_cb(stream_id, data, len, is_last);
    }
}

static void pouch_downlink_entries_push(struct pouch_bufview *v)
{
    const uint8_t *path;
    uint8_t path_len;
    const uint8_t *data;
    uint16_t data_len;
    uint16_t content_type;
    uint8_t path_null_term[256];

    while (pouch_bufview_available(v))
    {
        data_len = pouch_bufview_read_be16(v);
        content_type = pouch_bufview_read_be16(v);
        path_len = pouch_bufview_read_byte(v);

        LOG_DBG("data_len %u", (unsigned int) data_len);
        LOG_DBG("content_type %s (%u)",
                entry_content_format_str(content_type),
                (unsigned int) content_type);
        LOG_DBG("path_len %u", (unsigned int) path_len);

        path = pouch_bufview_read(v, path_len);
        data = pouch_bufview_read(v, data_len);

        /* Copy path to add NULL terminator */
        memcpy(path_null_term, path, path_len);
        path_null_term[path_len] = '\0';

        downlink_start(0, path_null_term, content_type);
        downlink_data(0, data, data_len, true);
    }
}

static void pouch_downlink_stream_push(struct pouch_bufview *v,
                                       unsigned int stream_id,
                                       bool is_first,
                                       bool is_last)
{
    const uint8_t *data;
    uint16_t data_len;

    if (is_first)
    {
        const uint8_t *path;
        uint8_t path_len;
        uint16_t content_type;
        uint8_t path_null_term[256];

        content_type = pouch_bufview_read_be16(v);
        path_len = pouch_bufview_read_byte(v);

        LOG_DBG("content_type %s (%d)", entry_content_format_str(content_type), (int) content_type);
        LOG_DBG("path_len %zu", path_len);

        path = pouch_bufview_read(v, path_len);

        /* Copy path to add NULL terminator */
        memcpy(path_null_term, path, path_len);
        path_null_term[path_len] = '\0';

        downlink_start(0, path_null_term, content_type);
    }

    data_len = pouch_bufview_available(v);
    data = pouch_bufview_read(v, data_len);

    downlink_data(stream_id, data, data_len, is_last);
}

void pouch_downlink_block_push(struct pouch_buf *pouch_buf)
{
    struct pouch_bufview v;
    pouch_bufview_init(&v, pouch_buf);

    uint16_t block_size;
    uint8_t stream_id;
    bool is_stream;
    bool is_first;
    bool is_last;
    block_decode_hdr(&v, &block_size, &stream_id, &is_stream, &is_first, &is_last);

    LOG_HEXDUMP_DBG(pouch_bufview_read(&v, 0), pouch_bufview_available(&v), "block bufview");

    if (is_stream)
    {
        pouch_downlink_stream_push(&v, stream_id, is_first, is_last);
    }
    else
    {
        pouch_downlink_entries_push(&v);
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
