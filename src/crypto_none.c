/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#include "crypto.h"

static const char *device_id;

int crypto_init(const struct pouch_config *config)
{
    if (device_id == NULL || strlen(device_id) > POUCH_DEVICE_ID_MAX_LEN)
    {
        return -EINVAL;
    }

    device_id = config->encryption.plaintext.device_id;

    return 0;
}


int crypto_pouch_session_start(void)
{
    return 0;
}

int crypto_session_info_get(struct session_info *session_info)
{
    session_info->
}

int crypto_header_get(struct encryption_info *encryption_info)
{
    encryption_info->Union_choice = encryption_info_union_plaintext_info_m_c;
    encryption_info->plaintext_info_m.id.len = strlen(device_id);
    encryption_info->plaintext_info_m.id.value = device_id;

    return 0;
}

struct pouch_buf *crypto_encrypt_block(struct pouch_buf *block)
{
    return block;
}
