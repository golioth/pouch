/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#include "block.h"
#include "entry.h"
#include <stdlib.h>
#include <string.h>
#include <zephyr/sys/byteorder.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(block);

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

static size_t block_downlink_push_one(struct pouch_block *block, const uint8_t *buf, size_t buf_len)
{
    const uint8_t *buf_start = buf;

    /* Header */
    while (buf_len)
    {
        if (block->header_len >= sizeof(block->header))
        {
            break;
        }

        block->header[block->header_len] = *buf;
        block->header_len++;

        buf++;
        buf_len--;

        if (block->header_len == sizeof(block->header))
        {
            /* Header complete */
            LOG_HEXDUMP_DBG(block->header, sizeof(block->header), "block header raw");

            block->size = sys_be16_to_cpu(block->size);
            LOG_DBG("size %d", block->size);
            LOG_DBG("id 0x%x", block->id);

            block->stream_id = (block->id & ~NO_MORE_DATA_MASK);

            break;
        }
    }

    /* Data */
    while (buf_len)
    {
        size_t remaining_length = block->size - block->data_len - sizeof(block->id);
        size_t to_consume = MIN(remaining_length, buf_len);

        LOG_DBG("block data chunk [%2d : %2d]",
                (int) block->data_len,
                (int) block->data_len + to_consume);
        LOG_HEXDUMP_DBG(buf, to_consume, "block data chunk");

        memcpy(&block->data[block->data_len], buf, to_consume);
        block->data_len += to_consume;

        buf += to_consume;
        buf_len -= to_consume;

        if (block->data_len + sizeof(block->id) >= block->size)
        {
            uint8_t stream_id = block->id & ~NO_MORE_DATA_MASK;
            bool is_last = block->id & NO_MORE_DATA_MASK;

            pouch_downlink_entries_push(block->data,
                                        block->data_len,
                                        stream_id != BLOCK_ID_ENTRY,
                                        is_last);

            LOG_DBG("Finished block");

            if (is_last && stream_id != BLOCK_ID_ENTRY)
            {
                block->stream_id = BLOCK_ID_ENTRY;
            }

            block->header_len = 0;
            block->data_len = 0;
            break;
        }
    }

    return buf - buf_start;
}

size_t block_downlink_push(struct pouch_block *block, const uint8_t *buf, size_t buf_len)
{
    const uint8_t *buf_start = buf;
    size_t consumed;

    while (buf_len)
    {
        consumed = block_downlink_push_one(block, buf, buf_len);

        buf += consumed;
        buf_len -= consumed;
    }

    return buf - buf_start;
}

void block_downlink_start(struct pouch_block *block)
{
    block->header_len = 0;
}

void block_downlink_finish(struct pouch_block *block) {}

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

static void finish(struct pouch_buf *block, uint8_t id, bool more_data)
{
    size_t size = block_size_get(block);
    pouch_buf_state_t state = buf_state_get(block);

    // Temporarily roll back the block to the initial state so we can write the block size:
    buf_restore(block, POUCH_BUF_STATE_INITIAL);

    write_block_header(block, size - sizeof(uint16_t), id, more_data);

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
