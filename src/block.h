/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#pragma once

#include <zephyr/kernel.h>
#include "buf.h"

#define BLOCK_ID_MASK 0x1f

struct pouch_buf *block_alloc(void);

struct pouch_buf *block_alloc_stream(uint8_t stream_id, bool first);

void block_free(struct pouch_buf *block);

size_t block_space_get(const struct pouch_buf *block);
size_t block_size_get(const struct pouch_buf *block);

void block_finish(struct pouch_buf *block);
void block_finish_stream(struct pouch_buf *block, uint8_t stream_id, bool last);
