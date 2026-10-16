/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal blockbuf stub for unit tests.  The real Zephyr blockbuf
 * (port/zephyr/blockbuf.c) defines a static k_mem_slab dimensioned
 * by CONFIG_POUCH_BLOCK_SIZE / CONFIG_POUCH_BLOCK_COUNT which pull
 * in more of the pouch core than these tests need.  This stub uses
 * malloc and avoids the Kconfig dependency.
 */

#include <pouch/blockbuf.h>

#include "../../../src/buf.h"

#include "stub_blockbuf.h"

#define STUB_BLOCKBUF_SIZE 4096

static size_t alloc_count;
static size_t free_count;
static size_t fail_from;

size_t stub_blockbuf_alloc_count(void)
{
    return alloc_count;
}

size_t stub_blockbuf_in_use(void)
{
    return alloc_count - free_count;
}

void stub_blockbuf_reset_counters(void)
{
    alloc_count = 0;
    free_count = 0;
    fail_from = 0;
}

void stub_blockbuf_fail_alloc_from(size_t nth)
{
    fail_from = nth;
}

struct pouch_buf *blockbuf_alloc(pouch_timeout_t timeout)
{
    (void) timeout;

    if (fail_from != 0 && alloc_count + 1 >= fail_from)
    {
        return NULL;
    }

    struct pouch_buf *buf = buf_alloc(STUB_BLOCKBUF_SIZE);
    if (buf != NULL)
    {
        alloc_count++;
    }

    return buf;
}

void blockbuf_free(struct pouch_buf *buf)
{
    free_count++;
    buf_free(buf);
}
