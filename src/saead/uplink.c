#include "uplink.h"
#include "session.h"
#include "../cert.h"
#include "../block.h"
#include <stdint.h>
#include <psa/crypto.h>
#include <zephyr/sys/byteorder.h>
#include <mbedtls/base64.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(saead_uplink, LOG_LEVEL_DBG);

static struct session uplink;

int uplink_session_start(psa_algorithm_t algorithm, psa_key_id_t private_key)
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
                                      MAX_BLOCK_SIZE_LOG,
                                      private_key,
                                      &pubkey,
                                      PSA_KEY_USAGE_ENCRYPT);
    if (err)
    {
        LOG_ERR("Session key generation failed (err: %d)", err);
        return err;
    }

    uplink.algorithm = algorithm;
    uplink.pouch.id = 0;
    atomic_set_bit(&uplink.flags, SESSION_VALID);
    atomic_set_bit(&uplink.flags, SESSION_ACTIVE);

    return 0;
}

int uplink_pouch_start(void)
{
    return session_pouch_start(&uplink, uplink.pouch.id + 1);
}

int uplink_header_get(struct saead_info *info)
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
    info->session.max_block_size_log = MAX_BLOCK_SIZE_LOG;

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

struct pouch_buf *uplink_encrypt_block(struct pouch_buf *block)
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

void uplink_session_end(void)
{
    session_end(&uplink);
}
