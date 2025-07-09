/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#include "crypto.h"
#include "saead/uplink.h"
#include "cert.h"

#if CONFIG_POUCH_ENCRYPTION_CHACHA20_POLY1305
#define ENCRYPTION_ALGORITHM PSA_ALG_CHACHA20_POLY1305
#elif CONFIG_POUCH_ENCRYPTION_AES_GCM
#define ENCRYPTION_ALGORITHM PSA_ALG_AES_GCM
#else
#error "unknown encryption algorithm"
#endif

static psa_key_id_t pkey;

int crypto_init(const struct pouch_config *config)
{
    if (config->encryption_type != POUCH_ENCRYPTION_SAEAD)
    {
        return -ENOTSUP;
    }

    if (config->encryption.saead.private_key == PSA_KEY_ID_NULL)
    {
        return -EINVAL;
    }

    int err = cert_device_set(&config->encryption.saead.certificate);
    if (err)
    {
        return err;
    }

    pkey = config->encryption.saead.private_key;

    return 0;
}

int crypto_session_start(void)
{
    return uplink_session_start(ENCRYPTION_ALGORITHM, pkey);
}

void crypto_session_end(void)
{
    uplink_session_end();
}

int crypto_pouch_start(void)
{
    return uplink_pouch_start();
}

int crypto_header_get(struct encryption_info *encryption_info)
{
    encryption_info->Union_choice = encryption_info_union_saead_info_m_c;
    return uplink_header_get(&encryption_info->saead_info_m);
}

struct pouch_buf *crypto_decrypt_block(struct pouch_buf *block)
{
    return block;
}

struct pouch_buf *crypto_encrypt_block(struct pouch_buf *block)
{
    return uplink_encrypt_block(block);
}
