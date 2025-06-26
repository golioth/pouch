/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#include "crypto.h"

int crypto_pouch_start(void)
{
    return 0;
}

int crypto_header_get(const struct pouch_config *config, struct encryption_info *encryption_info)
{
    if (config->encryption.plaintext.device_id == NULL)
    {
        return -EINVAL;
    }

    encryption_info->Union_choice = encryption_info_union_plaintext_info_m_c;
    encryption_info->plaintext_info_m.id.len = strlen(config->encryption.plaintext.device_id);
    encryption_info->plaintext_info_m.id.value = config->encryption.plaintext.device_id;

    return 0;
}

struct pouch_buf *crypto_decrypt_block(struct pouch_buf *block)
{
    return block;
}

struct pouch_buf *crypto_encrypt_block(struct pouch_buf *block)
{
    return block;
}
