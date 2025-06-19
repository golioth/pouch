/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#pragma once

#include <zephyr/kernel.h>
#include "buf.h"

#define BLOCK_ID_MASK 0x7f

struct pouch_block
{
    union
    {
        uint8_t header[3];
        struct
        {
            uint16_t size;
            uint8_t id;
        } __packed;
    };

    /* Active stream id */
    uint8_t stream_id;

    uint8_t header_len;
    size_t data_len;
    uint8_t data[CONFIG_POUCH_BLOCK_SIZE];

    size_t entry_len;
};

void block_downlink_start(struct pouch_block *block);
size_t block_downlink_push(struct pouch_block *block, const uint8_t *buf, size_t buf_len);
void block_downlink_finish(struct pouch_block *block);

struct pouch_buf *block_alloc(void);

struct pouch_buf *block_alloc_stream(uint8_t stream_id);

void block_free(struct pouch_buf *block);

size_t block_space_get(const struct pouch_buf *block);
size_t block_size_get(const struct pouch_buf *block);

void block_finish(struct pouch_buf *block);
void block_finish_stream(struct pouch_buf *block, uint8_t stream_id, bool more_data);
