/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#include "block.h"
#include <stdlib.h>
#include <string.h>
#include <zephyr/sys/byteorder.h>

#define HEADER_OVERHEAD 2

#define BLOCK_SIZE (HEADER_OVERHEAD + CONFIG_POUCH_BLOCK_SIZE)

enum block_type
{
    BLOCK_TYPE_ENTRY,
};

static inline void write_block_size(struct pouch_buf *block, size_t size)
{
    sys_put_be16(size, buf_claim(block, sizeof(uint16_t)));
}

static void write_block_header(struct pouch_buf *block, enum block_type type, uint8_t stream_id)
{
    write_block_size(block, 0);
    *buf_claim(block, 1) = type;
}

size_t block_space_get(const struct pouch_buf *block)
{
    return BLOCK_SIZE - buf_size_get(block);
}

struct pouch_buf *block_alloc(void)
{
    struct pouch_buf *block = buf_alloc(BLOCK_SIZE);
    if (block)
    {
        write_block_header(block, BLOCK_TYPE_ENTRY, 0);
    }

    return block;
}

void block_free(struct pouch_buf *block)
{
    buf_free(block);
}

void block_finish(struct pouch_buf *block)
{
    size_t size = buf_size_get(block);
    pouch_buf_state_t state = buf_state_get(block);

    // Temporarily roll back the block to the initial state so we can write the block size:
    buf_restore(block, POUCH_BUF_STATE_INITIAL);
    write_block_size(block, size - HEADER_OVERHEAD);

    // Restore:
    buf_restore(block, state);
}
