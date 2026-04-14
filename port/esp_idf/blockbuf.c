/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <pouch/blockbuf.h>
#include "../../src/block.h"

struct pouch_buf *blockbuf_alloc(pouch_timeout_t timeout)
{
    struct pouch_buf *buf = malloc(POUCH_BUF_OVERHEAD + MAX_PLAINTEXT_BLOCK_SIZE);
    if (buf == NULL)
    {
        return NULL;
    }

    return buf;
}

void blockbuf_free(struct pouch_buf *buf)
{
    free(buf);
}
