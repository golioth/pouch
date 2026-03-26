/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>

#include <zephyr/kernel.h>

struct block;

struct block *block_alloc(void *user_data, k_timeout_t timeout);
void block_free(struct block *block);
size_t block_length(const struct block *block);
void block_mark_last(struct block *block);
bool block_is_last(const struct block *block);
void block_append(struct block *block, const void *data, size_t data_len);
int block_get(const struct block *block, size_t offset, void *buf, size_t len);
