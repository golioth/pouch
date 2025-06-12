/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#include "block.h"
#include <stdlib.h>
#include <string.h>
#include <zephyr/sys/byteorder.h>

/* Block format:
 *
 * Multi byte fields are big-endian
 *
 *    |   0   |   1   |   2   |
 *    +-----------------------+
 *  0 |      size^    |   id  |
 *    +-----------------------+
 *  3 | data         ...      |
 *    +-----------------------+
 *
 *  ^ size is the number of bytes in the block, *not*
 *    including the size field.
 */

/** Special block ID for entry blocks */
#define BLOCK_ID_ENTRY 0x00

/** Mask for ID field indicating that this is the last block in the stream */
#define NO_MORE_DATA_MASK 0x80

#define BLOCK_HEADER_SIZE (sizeof(uint16_t) + 1)

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
    return buf_size_get(block);
}

struct pouch_buf *block_alloc(void)
{
    struct pouch_buf *block = buf_alloc(CONFIG_POUCH_BLOCK_SIZE);
    if (block != NULL)
    {
        write_block_header(block, 0, BLOCK_ID_ENTRY, false);
    }

    return block;
}

struct pouch_buf *block_alloc_stream(uint8_t stream_id)
{
    struct pouch_buf *block = buf_alloc(CONFIG_POUCH_BLOCK_SIZE);
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

void block_finish(struct pouch_buf *block, bool more_data)
{
    size_t size = block_size_get(block);
    pouch_buf_state_t state = buf_state_get(block);

    // Temporarily roll back the block to the initial state so we can write the block size:
    buf_restore(block, POUCH_BUF_STATE_INITIAL);

    uint8_t id = *buf_next(block) & BLOCK_ID_MASK;

    write_block_header(block, size - sizeof(uint16_t), id, more_data);

    // Restore:
    buf_restore(block, state);
}

const void *block_payload_get(struct pouch_buf *block, size_t *len)
{
    *len = block_size_get(block) - BLOCK_HEADER_SIZE;

    buf_restore(block, POUCH_BUF_STATE_INITIAL);
    buf_claim(block, BLOCK_HEADER_SIZE);

    return buf_claim(block, *len);
}

void block_header_copy(struct pouch_buf *to, const struct pouch_buf *from)
{
    pouch_buf_state_t state = buf_state_get(to);
    buf_restore(to, POUCH_BUF_STATE_INITIAL);

    buf_read(from, buf_claim(to, BLOCK_HEADER_SIZE), BLOCK_HEADER_SIZE, 0);

    buf_restore(to, state);
}

bool block_has_more_data(const struct pouch_buf *block)
{
    uint8_t id;
    buf_read(block, &id, 1, 2);
    return (id & NO_MORE_DATA_MASK) == 0;
}
