/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#pragma once

#include <zephyr/kernel.h>
#include "buf.h"

struct pouch_buf *block_alloc(k_timeout_t timeout);
struct pouch_buf *block_alloc_stream(uint8_t stream_id, k_timeout_t timeout);

void block_free(struct pouch_buf *block);

size_t block_space_get(const struct pouch_buf *block);

void block_finish(struct pouch_buf *block);
void block_finish_stream(struct pouch_buf *block, bool end_of_stream);

size_t block_write(struct pouch_buf *block, const uint8_t *data, size_t len);
