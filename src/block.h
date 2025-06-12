/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#pragma once

#include <zephyr/kernel.h>
#include "buf.h"

#define BLOCK_ID_MASK 0x7f

struct pouch_buf *block_alloc(void);

struct pouch_buf *block_alloc_stream(uint8_t stream_id);

void block_free(struct pouch_buf *block);

size_t block_space_get(const struct pouch_buf *block);
size_t block_size_get(const struct pouch_buf *block);

void block_finish(struct pouch_buf *block, bool more_data);

const void *block_payload_get(struct pouch_buf *block, size_t *len);
void block_header_copy(struct pouch_buf *to, const struct pouch_buf *from);
bool block_has_more_data(const struct pouch_buf *block);
