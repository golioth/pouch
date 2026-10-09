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

/** Zero the alloc and free counters and stop failing allocations. */
void stub_blockbuf_reset_counters(void);

/**
 * Make blockbuf_alloc() return NULL from its @p nth call on (1-based, counted since the last
 * reset). 0 disables.
 */
void stub_blockbuf_fail_alloc_from(size_t nth);
