/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#pragma once

#include <zephyr/kernel.h>
#include "buf.h"

struct pouch_buf *block_alloc(void);

void block_free(struct pouch_buf *block);

size_t block_space_get(const struct pouch_buf *block);

void block_finish(struct pouch_buf *block);
