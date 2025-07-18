/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include "buf.h"

#define BLOCK_ID_MASK 0x1f

/** Log2 of max block size */
#define MAX_BLOCK_SIZE_LOG LOG2(CONFIG_POUCH_BLOCK_SIZE)
/** Rounded maximum block size */
#define MAX_BLOCK_SIZE (1 << MAX_BLOCK_SIZE_LOG)

void block_decode_hdr(struct pouch_bufview *v,
                      uint16_t *block_size,
                      uint8_t *stream_id,
                      bool *is_stream,
                      bool *is_first,
                      bool *is_last);

struct pouch_buf *block_alloc(void);

struct pouch_buf *block_alloc_stream(uint8_t stream_id, bool first);

void block_free(struct pouch_buf *block);

size_t block_space_get(const struct pouch_buf *block);
size_t block_size_get(const struct pouch_buf *block);
void block_size_write(struct pouch_buf *block, uint16_t size);

void block_finish(struct pouch_buf *block);
void block_finish_stream(struct pouch_buf *block, uint8_t stream_id, bool last);
