/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "crypto.h"
#include <string.h>
#include "block.h"

static const char *device_id;

int crypto_init(const struct pouch_config *config)
{
    if (config->device_id == NULL)
    {
        return -EINVAL;
    }

    if (strlen(config->device_id) > POUCH_DEVICE_ID_MAX_LEN)
    {
        return -EINVAL;
    }

    device_id = config->device_id;

    return 0;
}

int crypto_session_start(void)
{
    return 0;
}

void crypto_session_end(void) {}

int crypto_downlink_start(const struct encryption_info *encryption_info)
{
    if (encryption_info->Union_choice != encryption_info_union_plaintext_info_m_c)
    {
        return -ENOTSUP;
    }

    return 0;
}

int crypto_pouch_start(void)
{
    return 0;
}

int crypto_header_get(struct encryption_info *encryption_info)
{
    encryption_info->Union_choice = encryption_info_union_plaintext_info_m_c;
    encryption_info->plaintext_info_m.id.len = strlen(device_id);
    encryption_info->plaintext_info_m.id.value = device_id;

    return 0;
}

struct pouch_buf *crypto_block_buf_alloc(void)
{
    return buf_alloc(MAX_PLAINTEXT_BLOCK_SIZE);
}

int crypto_decrypt_block(const struct pouch_buf *block, struct pouch_buf *decrypted)
{
    struct pouch_bufview v;
    pouch_bufview_init(&v, block);

    size_t payload_len = pouch_bufview_available(&v);
    pouch_bufview_memcpy(&v, buf_claim(decrypted, payload_len), payload_len);
    return 0;
}

struct pouch_buf *crypto_encrypt_block(struct pouch_buf *block)
{
    // Have to copy this to get a buffer from the heap for transport:
    struct pouch_buf *encrypted = buf_alloc(MAX_CIPHERTEXT_BLOCK_SIZE);
    if (encrypted != NULL)
    {
        struct pouch_bufview v;
        pouch_bufview_init(&v, block);

        size_t size = pouch_bufview_available(&v);
        pouch_bufview_memcpy(&v, buf_claim(encrypted, size), size);
    }
    block_free(block);

    return encrypted;
}
