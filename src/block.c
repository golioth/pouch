/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#include "block.h"
#include <stdlib.h>
#include <string.h>
#include <zephyr/sys/byteorder.h>

#define HEADER_OVERHEAD 2

#define BLOCK_SIZE (HEADER_OVERHEAD + CONFIG_POUCH_BLOCK_SIZE)

/** Special block ID for entry blocks */
#define BLOCK_ID_ENTRY 0x00

/** Mask for ID field indicating that this is the last block in the stream */
#define NO_MORE_DATA_MASK 0x80

static void write_block_header(struct pouch_buf *block, size_t size, uint8_t id, bool more_data)
{
    if (!more_data)
    {
        id |= NO_MORE_DATA_MASK;
    }

    sys_put_be16(size, buf_claim(block, sizeof(uint16_t)));
    *buf_claim(block, 1) = id;
}

size_t block_space_get(const struct pouch_buf *block)
{
    return CONFIG_POUCH_BLOCK_SIZE - block_size_get(block);
}

size_t block_size_get(const struct pouch_buf *block)
{
    return buf_size_get(block) - HEADER_OVERHEAD;
}

struct pouch_buf *block_alloc(void)
{
    struct pouch_buf *block = buf_alloc(BLOCK_SIZE);
    if (block != NULL)
    {
        write_block_header(block, 0, BLOCK_ID_ENTRY, false);
    }

    return block;
}

struct pouch_buf *block_alloc_stream(uint8_t stream_id)
{
    struct pouch_buf *block = buf_alloc(BLOCK_SIZE);
    if (block != NULL)
    {
        write_block_header(block, 0, stream_id, true);
    }

    return block;
}

void block_free(struct pouch_buf *block)
{
    buf_free(block);
}

static void finish(struct pouch_buf *block, uint8_t id, bool more_data)
{
    size_t size = block_size_get(block);
    pouch_buf_state_t state = buf_state_get(block);

    // Temporarily roll back the block to the initial state so we can write the block size:
    buf_restore(block, POUCH_BUF_STATE_INITIAL);

    write_block_header(block, size, id, more_data);

    // Restore:
    buf_restore(block, state);
}

void block_finish(struct pouch_buf *block)
{
    finish(block, BLOCK_ID_ENTRY, false);
}

void block_finish_stream(struct pouch_buf *block, uint8_t stream_id, bool more_data)
{
    finish(block, stream_id, more_data);
}
