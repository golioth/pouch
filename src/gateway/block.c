/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/sflist.h>

#include "block.h"

enum block_flags
{
    BLOCK_LAST = 0,
};

struct block
{
    sys_sfnode_t node;
    void *user_data;
    struct
    {
        uint8_t is_last : 1;
    } flags;
    size_t len;
    uint8_t data[CONFIG_GOLIOTH_BLOCKWISE_UPLOAD_MAX_BLOCK_SIZE];
};

K_MEM_SLAB_DEFINE_STATIC(block_slab, sizeof(struct block), CONFIG_POUCH_GATEWAY_NUM_BLOCKS, 4);

struct block *block_alloc(void *user_data, k_timeout_t timeout)
{
    struct block *block = NULL;
    int err = k_mem_slab_alloc(&block_slab, (void **) &block, timeout);
    if (0 == err)
    {
        block->flags.is_last = 0;
        block->len = 0;
        block->user_data = user_data;
    }

    return block;
}

void block_free(struct block *block)
{
    k_mem_slab_free(&block_slab, block);
}

size_t block_length(const struct block *block)
{
    return block->len;
}

void block_mark_last(struct block *block)
{
    block->flags.is_last = true;
}

bool block_is_last(const struct block *block)
{
    return 1 == block->flags.is_last;
}

void block_append(struct block *block, const void *data, size_t data_len)
{
    memcpy(block->data + block->len, data, data_len);
    block->len += data_len;
}

int block_get(const struct block *block, size_t offset, void *buf, size_t len)
{
    if (offset + len > CONFIG_GOLIOTH_BLOCKWISE_UPLOAD_MAX_BLOCK_SIZE)
    {
        return -EINVAL;
    }

    memcpy(buf, block->data + offset, len);

    return 0;
}
