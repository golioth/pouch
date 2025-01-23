/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#include "encrypt.h"
#include "uplink.h"

#include <zcbor_encode.h>

#define ENCRYPTION_TYPE_NONE 0

bool encrypt_pouch_header_encryption_info_encode(zcbor_state_t *zse)
{
    return zcbor_uint32_put(zse, ENCRYPTION_TYPE_NONE);
}

int encrypt_block(const struct block *block, k_timeout_t timeout)
{
    return uplink_enqueue(block->buf, block->bytes, timeout);
}
