/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#include "crypto.h"

BUILD_ASSERT(1 != sizeof(CONFIG_POUCH_DEVICE_ID), "CONFIG_POUCH_DEVICE_ID must be provided");

int crypto_pouch_start(void)
{
    return 0;
}

int crypto_header_get(struct encryption_info *encryption_info)
{
    encryption_info->Union_choice = encryption_info_union_plaintext_info_m_c;
    encryption_info->plaintext_info_m.id.len = sizeof(CONFIG_POUCH_DEVICE_ID) - 1;
    encryption_info->plaintext_info_m.id.value = CONFIG_POUCH_DEVICE_ID;

    return 0;
}

struct pouch_buf *crypto_encrypt_block(struct pouch_buf *block)
{
    return block;
}
