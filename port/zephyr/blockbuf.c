/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "../../src/blockbuf.h"
#include "../../src/block.h"
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

K_MEM_SLAB_DEFINE(blockbuf,
                  WB_UP(POUCH_BUF_OVERHEAD + MAX_PLAINTEXT_BLOCK_SIZE),
                  CONFIG_POUCH_BLOCK_COUNT,
                  4);

struct pouch_buf *blockbuf_alloc(k_timeout_t timeout)
{
    struct pouch_buf *buf = NULL;
    int err = k_mem_slab_alloc(&blockbuf, (void **) &buf, timeout);
    if (err || buf == NULL)
    {
        return NULL;
    }

    buf_restore(buf, POUCH_BUF_STATE_INITIAL);

    return buf;
}

void blockbuf_free(struct pouch_buf *buf)
{
    k_mem_slab_free(&blockbuf, buf);
}
