/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#pragma once

#include <zephyr/kernel.h>

/** Single pouch data block */
struct block
{
    /** Data buffer */
    uint8_t buf[CONFIG_POUCH_BLOCK_SIZE];
    /** Number of bytes in the buffer */
    size_t bytes;
    /** Mutex to protect the block */
    struct k_mutex mutex;
};

/** Take the block mutex */
int block_lock(k_timeout_t timeout);

/** Release the block mutex */
void block_release(void);

/**
 * Write data to the block.
 *
 * If the block runs out of space, it'll be encrypted and a new block is started.
 */
int block_write(const uint8_t *data, size_t len, k_timeout_t timeout);

/** Close out the current block, passing it on to encryption. */
int block_flush(k_timeout_t timeout);
