#include "uplink.h"
#include "session.h"
#include "../cert.h"
#include <stdint.h>
#include <psa/crypto.h>
#include <zephyr/sys/byteorder.h>
#include <mbedtls/base64.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(saead_uplink, LOG_LEVEL_DBG);

static struct session uplink;

static int session_id_generate(struct session *session)
{
    psa_status_t status;

    session->id.initiator = POUCH_ROLE_DEVICE;

    if (session->id.type == SESSION_ID_TYPE_RANDOM)
    {
        status = psa_generate_random(session->id.random, sizeof(session->id.random));
        if (status != PSA_SUCCESS)
        {
            LOG_ERR("Failed generating session ID: %d", status);
            return -EIO;
        }

        return 0;
    }

    status = psa_generate_random(session->id.sequential.tag, sizeof(session->id.sequential.tag));
    if (status != PSA_SUCCESS)
    {
        LOG_ERR("Failed generating session ID tag: %d", status);
        return -EIO;
    }

    session->id.sequential.seqnum++;

    return 0;
}

int uplink_session_start(psa_algorithm_t algorithm, psa_key_id_t private_key)
{
    session_init(&uplink);
    uplink.algorithm = algorithm;

    int err = session_id_generate(&uplink);
    if (err)
    {
        LOG_ERR("Session ID generation failed (err: %d)", err);
        return err;
    }

    err = session_key_generate(&uplink, private_key, PSA_KEY_USAGE_ENCRYPT);
    if (err)
    {
        LOG_ERR("Session key generation failed (err: %d)", err);
        return err;
    }

    atomic_set_bit(&uplink.flags, SESSION_VALID);
    atomic_set_bit(&uplink.flags, SESSION_ACTIVE);

    return 0;
}

pouch_id_t uplink_pouch_start(void)
{
    // Only bump the ID if this isn't the first pouch in the session.
    // The pouch ID is set to 0 when initializing the session.
    if (atomic_test_and_set_bit(&uplink.flags, SESSION_HAS_POUCH))
    {
        uplink.pouch.id++;
    }

    uplink.pouch.block_index = 0;

    return uplink.pouch.id;
}

int uplink_session_info_get(struct session_info *info)
{
    if (!atomic_test_bit(&uplink.flags, SESSION_ACTIVE))
    {
        LOG_ERR("Session not active");
        return -ENOTCONN;
    }

    info->version = POUCH_VERSION;
    info->algorithm_choice = (uplink.algorithm == PSA_ALG_CHACHA20_POLY1305)
        ? session_info_algorithm_chacha20_poly1305_m_c
        : session_info_algorithm_aes_gcm_m_c;

    if (uplink.id.type == SESSION_ID_TYPE_SEQUENTIAL)
    {
        info->session_id_m.union_choice = session_id_union_session_id_sequential_m_c;
        info->session_id_m.session_id_sequential_m.sequence_number = uplink.id.sequential.seqnum;
        info->session_id_m.session_id_sequential_m.tag.value = uplink.id.sequential.tag;
        info->session_id_m.session_id_sequential_m.tag.len = sizeof(uplink.id.sequential.tag);
    }
    else
    {
        info->session_id_m.union_choice = session_id_union_session_id_random_m_c;
        info->session_id_m.session_id_random_m.value = uplink.id.random;
        info->session_id_m.session_id_random_m.len = sizeof(uplink.id.random);
    }

    // only ECDH is supported for now:
    info->key_info_m.union_choice = key_info_union_ecdh_m_c;
    info->key_info_m.ecdh_m.cert_id.value = cert_ref_get();
    info->key_info_m.ecdh_m.cert_id.len = CERT_REF_SHORT_LEN;

    return 0;
}

bool uplink_session_matches(const session_id_t *id, psa_algorithm_t algorithm)
{
    return atomic_test_bit(&uplink.flags, SESSION_VALID) && session_id_is_equal(id, &uplink.id)
        && uplink.algorithm == algorithm;
}

int uplink_session_key_copy(psa_key_id_t *key, psa_key_usage_t usage)
{
    if (!atomic_test_bit(&uplink.flags, SESSION_ACTIVE))
    {
        return -EIO;
    }

    psa_key_attributes_t attrs = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_usage_flags(&attrs, usage);

    psa_status_t status = psa_copy_key(uplink.key, &attrs, key);
    if (status != PSA_SUCCESS)
    {
        return -EIO;
    }

    return 0;
}

struct pouch_buf *uplink_encrypt_block(struct pouch_buf *block)
{
    struct pouch_buf *encrypted = NULL;
    if (atomic_test_bit(&uplink.flags, SESSION_ACTIVE))
    {
        encrypted = session_encrypt_block(&uplink, block);
    }
    else
    {
        LOG_WRN("Not in a session");
    }

    buf_free(block);

    return encrypted;
}

void uplink_session_end(void)
{
    session_end(&uplink);
}
