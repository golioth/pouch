/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#include "crypto.h"
#include "saead/uplink.h"
#include "saead/downlink.h"
#include "cert.h"

#if CONFIG_POUCH_ENCRYPTION_CHACHA20_POLY1305
#define ENCRYPTION_ALGORITHM PSA_ALG_CHACHA20_POLY1305
#elif CONFIG_POUCH_ENCRYPTION_AES_GCM
#define ENCRYPTION_ALGORITHM PSA_ALG_GCM
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

int crypto_downlink_start(const struct encryption_info *encryption_info)
{
    if (encryption_info->Union_choice != encryption_info_union_saead_info_m_c)
    {
        return -ENOTSUP;
    }

    const struct session_info *session = &encryption_info->saead_info_m.session;

    struct session_id id = {
        .initiator = session->initiator_choice == session_info_initiator_device_m_c
            ? POUCH_ROLE_DEVICE
            : POUCH_ROLE_SERVER,
        .type = session->id_choice == session_info_id_session_id_random_m_c
            ? SESSION_ID_TYPE_RANDOM
            : SESSION_ID_TYPE_SEQUENTIAL,
    };
    if (id.type == SESSION_ID_TYPE_RANDOM)
    {
        if (session->session_id_random_m.len != sizeof(id.value.random))
        {
            return -EINVAL;
        }

        memcpy(id.value.random, session->session_id_random_m.value, sizeof(id.value.random));
    }
    else
    {
        if (session->session_id_sequential_m.tag.len != sizeof(id.value.sequential.tag))
        {
            return -EINVAL;
        }

        memcpy(id.value.sequential.tag,
               session->session_id_sequential_m.tag.value,
               sizeof(id.value.sequential.tag));
        id.value.sequential.seqnum = session->session_id_sequential_m.seq;
    }

    int err = saead_downlink_session_start(
        &id,
        session->algorithm_choice == session_info_algorithm_aes_gcm_m_c ? PSA_ALG_GCM
                                                                        : PSA_ALG_CHACHA20_POLY1305,
        session->max_block_size_log,
        pkey);
    if (err)
    {
        return err;
    }

    return saead_downlink_pouch_start(encryption_info->saead_info_m.pouch_id);
}

int crypto_session_start(void)
{
    return saead_uplink_session_start(ENCRYPTION_ALGORITHM, pkey);
}

void crypto_session_end(void)
{
    saead_uplink_session_end();
    saead_downlink_session_end();
}

int crypto_pouch_start(void)
{
    return saead_uplink_pouch_start();
}

int crypto_header_get(struct encryption_info *encryption_info)
{
    encryption_info->Union_choice = encryption_info_union_saead_info_m_c;
    return saead_uplink_header_get(&encryption_info->saead_info_m);
}

struct pouch_buf *crypto_decrypt_block(struct pouch_buf *block)
{
    return saead_downlink_block_decrypt(block);
}

struct pouch_buf *crypto_encrypt_block(struct pouch_buf *block)
{
    return saead_uplink_encrypt_block(block);
}
