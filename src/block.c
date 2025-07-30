/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
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

/** Mask for ID field indicating that this is the first block in the stream */
#define FIRST_DATA_MASK 0x40

/** Mask for ID field indicating that this is the last block in the stream */
#define LAST_DATA_MASK 0x80

void block_decode_hdr(struct pouch_bufview *v,
                      uint16_t *block_size,
                      uint8_t *stream_id,
                      bool *is_stream,
                      bool *is_first,
                      bool *is_last)
{
    __ASSERT_NO_MSG(v->offset == 0);

    *block_size = pouch_bufview_read_be16(v);

    uint8_t id = pouch_bufview_read_byte(v);

    *stream_id = id & BLOCK_ID_MASK;
    *is_stream = (*stream_id) != BLOCK_ID_ENTRY;
    *is_first = id & FIRST_DATA_MASK;
    *is_last = id & LAST_DATA_MASK;
}

static void update_block_header(struct pouch_buf *block, size_t size, uint8_t flags)
{
    sys_put_be16(size, buf_claim(block, sizeof(uint16_t)));
    *buf_claim(block, 1) |= flags;
}

static void write_block_header(struct pouch_buf *block, size_t size, uint8_t id, uint8_t flags)
{
    sys_put_be16(size, buf_claim(block, sizeof(uint16_t)));
    *buf_claim(block, 1) = id | flags;
}

size_t block_space_get(const struct pouch_buf *block)
{
    return MAX_PLAINTEXT_BLOCK_SIZE - block_size_get(block);
}

size_t block_size_get(const struct pouch_buf *block)
{
    return buf_size_get(block);
}

void block_size_write(struct pouch_buf *block, uint16_t size)
{
    sys_put_be16(size, buf_claim(block, sizeof(uint16_t)));
}

struct pouch_buf *block_alloc(void)
{
    struct pouch_buf *block = buf_alloc(MAX_PLAINTEXT_BLOCK_SIZE);
    if (block != NULL)
    {
        write_block_header(block, 0, BLOCK_ID_ENTRY, FIRST_DATA_MASK | LAST_DATA_MASK);
    }

    return block;
}

struct pouch_buf *block_alloc_stream(uint8_t stream_id, bool first)
{
    struct pouch_buf *block = buf_alloc(MAX_PLAINTEXT_BLOCK_SIZE);
    if (block != NULL)
    {
        write_block_header(block, 0, stream_id, first ? FIRST_DATA_MASK : 0);
    }

    return block;
}

void block_free(struct pouch_buf *block)
{
    buf_free(block);
}

static void finish(struct pouch_buf *block, uint8_t id, uint8_t flags)
{
    size_t size = block_size_get(block);
    pouch_buf_state_t state = buf_state_get(block);

    // Temporarily roll back the block to the initial state so we can write the block size:
    buf_restore(block, POUCH_BUF_STATE_INITIAL);

    update_block_header(block, size - sizeof(uint16_t), flags);

    // Restore:
    buf_restore(block, state);
}

void block_finish(struct pouch_buf *block)
{
    finish(block, BLOCK_ID_ENTRY, FIRST_DATA_MASK | LAST_DATA_MASK);
}

void block_finish_stream(struct pouch_buf *block, uint8_t stream_id, bool last)
{
    finish(block, stream_id, last ? LAST_DATA_MASK : 0);
}
