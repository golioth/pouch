/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stddef.h>

/** Number of blockbuf_alloc() calls that returned a buffer. */
size_t stub_blockbuf_alloc_count(void);

/** Buffers returned by blockbuf_alloc() and not yet freed. */
size_t stub_blockbuf_in_use(void);

/** Zero the alloc and free counters. */
void stub_blockbuf_reset_counters(void);
