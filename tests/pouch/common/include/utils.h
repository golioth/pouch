/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <zephyr/sys/byteorder.h>
#include <zcbor_decode.h>
#include <zephyr/ztest.h>

struct block
{
    uint8_t id;
    bool more_data;
    size_t data_len;
    uint8_t *data;
};

/** Test utility for pulling a block out of the given buffer */
static inline void pull_block(uint8_t **buf, struct block *block)
{
    uint8_t *data = *buf;
    block->data_len = sys_get_be16(&data[0]) - 1;
    block->id = data[2] & 0x7f;
    block->more_data = !(data[2] & 0x80);
    block->data = &data[3];
    *buf = &data[3 + block->data_len];
}

struct stream_block
{
    struct block block;
    uint16_t content_type;
    size_t path_len;
    uint8_t *path;
    uint8_t *data;
    size_t data_len;
};

/** Test utility for pulling a stream block out of the given buffer */
static inline void pull_stream_block(uint8_t **buf, struct stream_block *block)
{
    pull_block(buf, &block->block);
    block->content_type = sys_get_be16(block->block.data);
    block->path_len = block->block.data[2];
    block->path = &block->block.data[3];
    block->data = &block->block.data[3 + block->path_len];
    block->data_len = block->block.data_len - 3 - block->path_len;
}


static inline uint8_t *skip_pouch_header(const uint8_t *buf, size_t *len)
{
    // skip the pouch header:
    ZCBOR_STATE_D(zsd, 2, buf, *len, 1, 0);

    zassert_true(zcbor_list_start_decode(zsd));
    while (!zcbor_array_at_end(zsd))
    {
        zcbor_any_skip(zsd, NULL);
    }
    zassert_true(zcbor_list_end_decode(zsd));

    uint8_t *block = (uint8_t *) zsd->payload;
    *len -= (block - buf);

    return block;
}
