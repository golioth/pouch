/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#include "crypto.h"

#ifndef CONFIG_POUCH_DEVICE_ID
#error "CONFIG_POUCH_DEVICE_ID must be defined"
#endif

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
