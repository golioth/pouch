/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "uplink.h"
#include "session.h"
#include "../cert.h"
#include "../block.h"
#include <stdint.h>
#include <psa/crypto.h>
#include <zephyr/sys/byteorder.h>
#include <mbedtls/base64.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(saead_uplink, CONFIG_POUCH_LOG_LEVEL);

static struct session uplink;

int saead_uplink_session_start(psa_algorithm_t algorithm, psa_key_id_t private_key)
{
    struct pubkey pubkey;

    uplink.flags = ATOMIC_INIT(0);

    // Sequential IDs require replay protection, which isn't supported yet:
    uplink.id.type = SESSION_ID_TYPE_RANDOM;

    int err = session_id_generate(&uplink.id);
    if (err)
    {
        LOG_ERR("Session ID generation failed (err: %d)", err);
        return err;
    }

    cert_server_key_get(&pubkey);

    uplink.key = session_key_generate(&uplink.id,
                                      algorithm,
                                      MAX_BLOCK_PAYLOAD_SIZE_LOG,
                                      private_key,
                                      &pubkey,
                                      PSA_KEY_USAGE_ENCRYPT);
    if (PSA_KEY_ID_NULL == uplink.key)
    {
        LOG_ERR("Session key generation failed");
        return -ENOENT;
    }

    uplink.algorithm = algorithm;
    uplink.pouch.id = 0;
    atomic_set_bit(&uplink.flags, SESSION_VALID);
    atomic_set_bit(&uplink.flags, SESSION_ACTIVE);

    return 0;
}

int saead_uplink_pouch_start(void)
{
    return session_pouch_start(&uplink, uplink.pouch.id + 1);
}

int saead_uplink_header_get(struct saead_info *info)
{
    if (!atomic_test_bit(&uplink.flags, SESSION_ACTIVE))
    {
        LOG_ERR("Not in a session");
        return -ENOTCONN;
    }

    if (uplink.id.type == SESSION_ID_TYPE_SEQUENTIAL)
    {
        info->session.id_choice = session_info_id_session_id_sequential_m_c;
        info->session.session_id_sequential_m.seq = uplink.id.value.sequential.seqnum;
        info->session.session_id_sequential_m.tag.value = uplink.id.value.sequential.tag;
        info->session.session_id_sequential_m.tag.len = sizeof(uplink.id.value.sequential.tag);
    }
    else
    {
        info->session.id_choice = session_info_id_session_id_random_m_c;
        info->session.session_id_random_m.value = uplink.id.value.random;
        info->session.session_id_random_m.len = sizeof(uplink.id.value.random);
    }

    info->session.algorithm_choice = (uplink.algorithm == PSA_ALG_CHACHA20_POLY1305)
        ? session_info_algorithm_chacha20_poly1305_m_c
        : session_info_algorithm_aes_gcm_m_c;
    info->session.initiator_choice = session_info_initiator_device_m_c;
    info->session.max_block_size_log = MAX_BLOCK_PAYLOAD_SIZE_LOG;

    const uint8_t *cert_ref = cert_ref_get();
    if (cert_ref == NULL)
    {
        return -EBUSY;
    }

    info->session.cert_ref.value = cert_ref;
    info->session.cert_ref.len = CERT_REF_SHORT_LEN;

    info->pouch_id = uplink.pouch.id;

    return 0;
}

struct pouch_buf *saead_uplink_encrypt_block(struct pouch_buf *block)
{
    if (!atomic_test_bit(&uplink.flags, SESSION_ACTIVE))
    {
        LOG_WRN("Not in a session");
        buf_free(block);
        return NULL;
    }

    struct pouch_buf *encrypted = session_encrypt_block(&uplink, block);

    buf_free(block);

    return encrypted;
}

void saead_uplink_session_end(void)
{
    session_end(&uplink);
}

bool saead_uplink_session_matches(const struct session_id *id,
                                  uint8_t max_block_size_log,
                                  psa_algorithm_t algorithm)
{
    return atomic_test_bit(&uplink.flags, SESSION_VALID) && session_id_is_equal(id, &uplink.id)
        && max_block_size_log == MAX_BLOCK_PAYLOAD_SIZE_LOG && uplink.algorithm == algorithm;
}

psa_key_id_t saead_uplink_session_key_copy(psa_key_usage_t usage)
{
    psa_key_id_t copy = PSA_KEY_ID_NULL;
    if (!atomic_test_bit(&uplink.flags, SESSION_VALID))
    {
        return PSA_KEY_ID_NULL;
    }

    psa_key_attributes_t attrs = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_usage_flags(&attrs, usage);

    psa_status_t status = psa_copy_key(uplink.key, &attrs, &copy);
    if (status != PSA_SUCCESS)
    {
        return PSA_KEY_ID_NULL;
    }

    return copy;
}
