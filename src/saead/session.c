#include "session.h"
#include "../block.h"
#include "../cert.h"
#include <stdint.h>
#include <sys/types.h>
#include <psa/crypto.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/base64.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(saead_session, LOG_LEVEL_DBG);

#define NONCE_LEN 12
#define AUTH_TAG_LEN 16
#define PUBKEY_LEN PSA_EXPORT_PUBLIC_KEY_MAX_SIZE
#define INFO_MAX_LEN 64
#define SAEAD_KEY_SIZE 32

#define SESSION_KEY_TYPE(alg) \
    ((alg) == PSA_ALG_CHACHA20_POLY1305 ? PSA_KEY_TYPE_CHACHA20 : PSA_KEY_TYPE_AES)

pouch_id_t pouch_id_alloc(struct session *session)
{
    // Only bump the ID if this isn't the first pouch in the session.
    // The pouch ID is set to 0 when initializing the session.
    if (atomic_test_and_set_bit(&session->flags, SESSION_HAS_POUCH))
    {
        session->pouch.id++;
    }

    session->pouch.block_index = 0;

    return session->pouch.id;
}

void session_init(struct session *session)
{
    memset(&session->pouch, 0, sizeof(session->pouch));
    session->key = PSA_KEY_ID_NULL;
    session->flags = ATOMIC_INIT(0);
}

void session_end(struct session *session)
{
    if (!atomic_test_and_clear_bit(&session->flags, SESSION_ACTIVE))
    {
        return;
    }

    (void) psa_destroy_key(session->key);
    session->key = PSA_KEY_ID_NULL;
}

static ssize_t session_key_info_build(const struct session *session, char *buf)
{
    char session_id[SESSION_ID_LEN * 2];
    size_t id_len = 0;
    int err = base64_encode(session_id,
                            sizeof(session_id),
                            &id_len,
                            session->id.raw,
                            sizeof(session->id.raw));
    if (err)
    {
        return -EIO;
    }

    session_id[id_len] = '\0';

    return sprintf(buf,
                   "E0:%c:%s:C%c%c",
                   session->id.initiator == POUCH_ROLE_DEVICE ? 'D' : 'S',
                   session_id,
                   session->algorithm == PSA_ALG_CHACHA20_POLY1305 ? 'C' : 'A',
                   session->id.type == SESSION_ID_TYPE_SEQUENTIAL ? 'S' : 'R');
}

int session_key_generate(struct session *session, psa_key_id_t private_key, psa_key_usage_t usage)
{
    psa_status_t status;
    psa_key_attributes_t key_attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_key_derivation_operation_t operation = PSA_KEY_DERIVATION_OPERATION_INIT;
    struct pubkey pubkey;

    // ECDH + HKDF with SHA-256:
    status = psa_key_derivation_setup(
        &operation,
        PSA_ALG_KEY_AGREEMENT(PSA_ALG_ECDH, PSA_ALG_HKDF(PSA_ALG_SHA_256)));
    if (status != PSA_SUCCESS)
    {
        LOG_ERR("Couldn't set up key derivation: %d", status);
        goto exit;
    }

    cert_server_key_get(&pubkey);
    if (pubkey.len == 0)
    {
        status = PSA_ERROR_DOES_NOT_EXIST;
        LOG_ERR("Couldn't get server key: %d", status);
        goto exit;
    }

    // Perform the ECDH key agreement, and feed the shared secret directly into the HKDF:
    status = psa_key_derivation_key_agreement(&operation,
                                              PSA_KEY_DERIVATION_INPUT_SECRET,
                                              private_key,
                                              pubkey.data,
                                              pubkey.len);
    if (status != PSA_SUCCESS)
    {
        LOG_ERR("Failed key agreement: %d", status);
        goto exit;
    }

    uint8_t info[INFO_MAX_LEN];
    ssize_t info_len = session_key_info_build(session, info);
    if (info_len < 0)
    {
        LOG_ERR("Failed session key build: %d", info_len);
        goto exit;
    }

    status =
        psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_INFO, info, info_len);
    if (status != PSA_SUCCESS)
    {
        LOG_ERR("Failed info input: %d", status);
        goto exit;
    }

    psa_set_key_usage_flags(&key_attributes, usage);
    psa_set_key_lifetime(&key_attributes, PSA_KEY_LIFETIME_VOLATILE);
    psa_set_key_algorithm(&key_attributes, session->algorithm);
    psa_set_key_type(&key_attributes, SESSION_KEY_TYPE(session->algorithm));
    psa_set_key_bits(&key_attributes, SAEAD_KEY_SIZE * 8);

    status = psa_key_derivation_output_key(&key_attributes, &operation, &session->key);
    if (status != PSA_SUCCESS)
    {
        LOG_ERR("Failed key derivation: %d", status);
        goto exit;
    }

exit:
    // Release the derivation context. Does not invalidate the key.
    (void) psa_key_derivation_abort(&operation);

    if (status != PSA_SUCCESS)
    {
        LOG_ERR("Key derivation failed: %d", status);
        return -EIO;
    }

    return 0;
}

static void nonce_generate(const struct session *session,
                           enum pouch_role sender,
                           uint8_t nonce[NONCE_LEN])
{
    sys_put_be16(session->pouch.id, &nonce[0]);
    sys_put_be16(session->pouch.block_index, &nonce[2]);
    nonce[4] = sender;
    memset(&nonce[5], 0, NONCE_LEN - 5);
}

struct pouch_buf *session_encrypt_block(struct session *session, struct pouch_buf *block)
{
    struct pouch_buf *encrypted = block_alloc();
    if (encrypted == NULL)
    {
        LOG_ERR("Couldn't allocate encrypted block");
        return NULL;
    }

    uint8_t nonce[NONCE_LEN];
    nonce_generate(session, POUCH_ROLE_DEVICE, nonce);

    LOG_INF("Session key: %d", session->key);
    size_t plaintext_len;
    const uint8_t *plaintext = block_payload_get(block, &plaintext_len);

    block_header_copy(encrypted, block);
    size_t block_space = block_space_get(encrypted);
    uint8_t *ciphertext = buf_next(encrypted);
    size_t cipher_len;

    psa_status_t status =
        psa_aead_encrypt(session->key,
                         session->algorithm,
                         nonce,
                         sizeof(nonce),
                         session->pouch.ad,
                         session->pouch.block_index > 0 ? sizeof(session->pouch.ad) : 0,
                         plaintext,
                         plaintext_len,
                         ciphertext,
                         block_space,
                         &cipher_len);
    if (status != PSA_SUCCESS)
    {
        LOG_ERR("Couldn't encrypt: %d", status);
        buf_free(encrypted);
        return NULL;
    }

    buf_claim(encrypted, cipher_len);
    block_finish(encrypted, block_has_more_data(block));

    // prepare for the next block:
    memcpy(&session->pouch.ad, &ciphertext[plaintext_len], AUTH_TAG_LEN);
    session->pouch.block_index++;

    return encrypted;
}

struct pouch_buf *session_decrypt_block(struct session *session, struct pouch_buf *block)
{
    struct pouch_buf *decrypted = block_alloc();
    if (decrypted == NULL)
    {
        buf_free(block);
        return NULL;
    }

    uint8_t nonce[NONCE_LEN];
    nonce_generate(session, POUCH_ROLE_DEVICE, nonce);

    size_t cipher_len;
    const uint8_t *ciphertext = block_payload_get(block, &cipher_len);

    block_header_copy(decrypted, block);
    size_t block_space = block_space_get(decrypted);
    uint8_t *plaintext = buf_next(decrypted);
    size_t plaintext_len;

    psa_status_t status =
        psa_aead_decrypt(session->key,
                         session->algorithm,
                         nonce,
                         sizeof(nonce),
                         session->pouch.ad,
                         session->pouch.block_index > 0 ? sizeof(session->pouch.ad) : 0,
                         ciphertext,
                         cipher_len,
                         plaintext,
                         block_space,
                         &plaintext_len);
    if (status != PSA_SUCCESS)
    {
        buf_free(decrypted);
        return NULL;
    }

    buf_claim(decrypted, plaintext_len);
    block_finish(decrypted, block_has_more_data(block));

    // prepare for the next block:
    memcpy(&session->pouch.ad, &ciphertext[plaintext_len], AUTH_TAG_LEN);
    session->pouch.block_index++;

    atomic_set_bit(&session->flags, SESSION_VALID);

    return decrypted;
}
