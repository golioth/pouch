/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#include "block.h"
#include <stdlib.h>
#include <string.h>
#include <zephyr/sys/byteorder.h>

#define HEADER_SIZE_ENTRY_BLOCK 3
#define HEADER_SIZE_STREAM_BLOCK 4

#define HEADER_OFFSET_SIZE 0
#define HEADER_OFFSET_TYPE 2
#define HEADER_OFFSET_STREAM_ID 3

enum block_type
{
    BLOCK_TYPE_ENTRY,
    BLOCK_TYPE_STREAM,
    BLOCK_TYPE_STREAM_END,
};

static inline size_t header_size(const struct pouch_buf *block)
{
    return (block->buf[HEADER_OFFSET_TYPE] == BLOCK_TYPE_ENTRY) ? HEADER_SIZE_ENTRY_BLOCK
                                                                : HEADER_SIZE_STREAM_BLOCK;
}

static inline void write_block_size(struct pouch_buf *block)
{
    // Block size does not include its own size:
    sys_put_be16(block->bytes - sizeof(uint16_t), &block->buf[HEADER_OFFSET_SIZE]);
}

static void write_block_header(struct pouch_buf *block, enum block_type type, uint8_t stream_id)
{
    block->buf[HEADER_OFFSET_TYPE] = type;

    switch (type)
    {
        case BLOCK_TYPE_ENTRY:
            block->bytes = HEADER_SIZE_ENTRY_BLOCK;
            break;
        case BLOCK_TYPE_STREAM:
        case BLOCK_TYPE_STREAM_END:
            block->buf[HEADER_OFFSET_STREAM_ID] = stream_id;
            block->bytes = HEADER_SIZE_STREAM_BLOCK;
            break;
    }

    write_block_size(block);
}

size_t block_space_get(const struct pouch_buf *block)
{
    return header_size(block) + CONFIG_POUCH_BLOCK_SIZE - block->bytes;
}

struct pouch_buf *block_alloc(k_timeout_t timeout)
{
    struct pouch_buf *block = buf_alloc(HEADER_SIZE_ENTRY_BLOCK + CONFIG_POUCH_BLOCK_SIZE, timeout);
    if (block)
    {
        write_block_header(block, BLOCK_TYPE_ENTRY, 0);
    }

    return block;
}

struct pouch_buf *block_alloc_stream(uint8_t stream_id, k_timeout_t timeout)
{
    struct pouch_buf *block =
        buf_alloc(HEADER_SIZE_STREAM_BLOCK + CONFIG_POUCH_BLOCK_SIZE, timeout);
    if (block)
    {
        write_block_header(block, BLOCK_TYPE_STREAM, stream_id);
    }

    return block;
}

void block_free(struct pouch_buf *block)
{
    buf_free(block);
}

void block_finish(struct pouch_buf *block)
{
    write_block_size(block);
}

void block_finish_stream(struct pouch_buf *block, bool end_of_stream)
{
    write_block_size(block);
    if (end_of_stream)
    {
        block->buf[HEADER_OFFSET_TYPE] = BLOCK_TYPE_STREAM_END;
    }
}

size_t block_write(struct pouch_buf *block, const uint8_t *data, size_t len)
{
    size_t space = block_space_get(block);
    len = MIN(len, space);

    buf_write(block, data, len);

    return len;
}
