/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#include "block.h"
#include "encrypt.h"

static struct block block;

static int finalize_block(k_timeout_t timeout)
{
    int err = encrypt_block(&block, timeout);
    if (err)
    {
        return err;
    }

    block.bytes = 0;

    return 0;
}

int block_lock(k_timeout_t timeout)
{
    int err = k_mutex_lock(&block.mutex, timeout);
    if (err)
    {
        return err;
    }

    return 0;
}

void block_release(void)
{
    k_mutex_unlock(&block.mutex);
}

int block_write(const uint8_t *data, size_t len, k_timeout_t timeout)
{
    size_t offset = 0;
    while (offset < len)
    {
        size_t write_len = MIN(len - offset, sizeof(block.buf) - block.bytes);

        memcpy(&block.buf[block.bytes], &data[offset], write_len);
        block.bytes += write_len;
        offset += write_len;

        if (block.bytes == sizeof(block.buf))
        {
            int err = finalize_block(timeout);
            if (err)
            {
                return err;
            }
        }
    }

    return 0;
}

int block_flush(k_timeout_t timeout)
{
    int err = block_lock(timeout);
    if (err)
    {
        return err;
    }

    if (block.bytes > 0)
    {
        err = finalize_block(timeout);
    }

    block_release();
    return err;
}
