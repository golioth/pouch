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

#define STUB_BLOCKBUF_SIZE 4096

struct pouch_buf *blockbuf_alloc(pouch_timeout_t timeout)
{
    (void) timeout;
    return buf_alloc(STUB_BLOCKBUF_SIZE);
}

void blockbuf_free(struct pouch_buf *buf)
{
    buf_free(buf);
}
