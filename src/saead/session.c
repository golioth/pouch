/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#include "session.h"

#include "../block.h"
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <psa/crypto.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/base64.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(saead_session, LOG_LEVEL_DBG);

/**
 * String length required to base64 encode a buffer of a given length, excluding the 0 terminator.
 */
#define BASE64_STRLEN(buflen) (4 * DIV_ROUND_UP(buflen, 3))

#define NONCE_LEN 12
#define INFO_MAX_LEN (12 + BASE64_STRLEN(SESSION_ID_LEN) + 1)
#define SAEAD_KEY_SIZE 32

#define SESSION_KEY_TYPE(alg) \
    ((alg) == PSA_ALG_CHACHA20_POLY1305 ? PSA_KEY_TYPE_CHACHA20 : PSA_KEY_TYPE_AES)

void session_end(struct session *session)
{
    if (!atomic_test_and_clear_bit(&session->flags, SESSION_ACTIVE))
    {
        return;
    }

    (void) psa_destroy_key(session->key);
    session->key = PSA_KEY_ID_NULL;
}

int session_id_generate(struct session_id *id)
{
    psa_status_t status;

    id->initiator = POUCH_ROLE_DEVICE;

    if (id->type == SESSION_ID_TYPE_RANDOM)
    {
        status = psa_generate_random(id->value.random, sizeof(id->value.random));
        if (status != PSA_SUCCESS)
        {
            LOG_ERR("Failed to generate session ID: %d", status);
            return -EIO;
        }

        return 0;
    }

    status = psa_generate_random(id->value.sequential.tag, sizeof(id->value.sequential.tag));
    if (status != PSA_SUCCESS)
    {
        LOG_ERR("Failed to generate session ID tag: %d", status);
        return -EIO;
    }

    id->value.sequential.seqnum++;

    return 0;
}

static ssize_t session_key_info_build(const struct session_id *id,
                                      psa_algorithm_t algorithm,
                                      uint8_t max_block_size_log,
                                      char *buf)
{
    char session_id[BASE64_STRLEN(SESSION_ID_LEN) + 1];
    size_t id_len = 0;

    int err = base64_encode(session_id,
                            sizeof(session_id),
                            &id_len,
                            (const void *) &id->value,
                            sizeof(id->value));
    if (err)
    {
        return -EIO;
    }

    session_id[id_len] = '\0';

    return sprintf(buf,
                   "E0:%c:%s:C%c%c:%02x",
                   id->initiator == POUCH_ROLE_DEVICE ? 'D' : 'S',
                   session_id,
                   algorithm == PSA_ALG_CHACHA20_POLY1305 ? 'C' : 'A',
                   id->type == SESSION_ID_TYPE_SEQUENTIAL ? 'S' : 'R',
                   max_block_size_log);
}

psa_key_id_t session_key_generate(const struct session_id *id,
                                  psa_algorithm_t algorithm,
                                  uint8_t max_block_size_log,
                                  psa_key_id_t private_key,
                                  const struct pubkey *pubkey,
                                  psa_key_usage_t usage)
{
    psa_key_derivation_operation_t operation = PSA_KEY_DERIVATION_OPERATION_INIT;
    psa_key_attributes_t key_attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_key_id_t key = PSA_KEY_ID_NULL;
    psa_status_t status;

    if (pubkey->len == 0)
    {
        LOG_ERR("Missing server key");
        return PSA_KEY_ID_NULL;
    }

    // ECDH + HKDF with SHA-256:
    status = psa_key_derivation_setup(
        &operation,
        PSA_ALG_KEY_AGREEMENT(PSA_ALG_ECDH, PSA_ALG_HKDF(PSA_ALG_SHA_256)));
    if (status != PSA_SUCCESS)
    {
        LOG_ERR("Couldn't set up key derivation: %d", status);
        goto exit;
    }

    // Perform the ECDH key agreement, and feed the shared secret directly into the HKDF:
    status = psa_key_derivation_key_agreement(&operation,
                                              PSA_KEY_DERIVATION_INPUT_SECRET,
                                              private_key,
                                              pubkey->data,
                                              pubkey->len);
    if (status != PSA_SUCCESS)
    {
        LOG_ERR("Failed key agreement: %d", status);
        goto exit;
    }

    uint8_t info[INFO_MAX_LEN];
    ssize_t info_len = session_key_info_build(id, algorithm, max_block_size_log, info);
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

    psa_set_key_lifetime(&key_attributes, PSA_KEY_LIFETIME_VOLATILE);
    psa_set_key_type(&key_attributes, SESSION_KEY_TYPE(algorithm));
    psa_set_key_bits(&key_attributes, SAEAD_KEY_SIZE * 8);
    psa_set_key_algorithm(&key_attributes, algorithm);
    psa_set_key_usage_flags(&key_attributes, usage);

    status = psa_key_derivation_output_key(&key_attributes, &operation, &key);
    if (status != PSA_SUCCESS)
    {
        LOG_ERR("Failed key derivation: %d", status);
        goto exit;
    }

exit:
    // Release the derivation context. Does not invalidate the key.
    (void) psa_key_derivation_abort(&operation);
    return key;
}

int session_pouch_start(struct session *session, pouch_id_t pouch_id)
{
    if (!atomic_test_bit(&session->flags, SESSION_ACTIVE))
    {
        return -EBUSY;
    }

    if (atomic_test_and_set_bit(&session->flags, SESSION_HAS_POUCH)
        && pouch_id <= session->pouch.id)
    {
        return -EINVAL;
    }

    session->pouch.id = pouch_id;
    session->pouch.block_index = 0;

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
    struct pouch_buf *encrypted = buf_alloc(MAX_BLOCK_SIZE + AUTH_TAG_LEN);
    if (encrypted == NULL)
    {
        LOG_ERR("Couldn't allocate encrypted block");
        return NULL;
    }

    uint8_t nonce[NONCE_LEN];
    nonce_generate(session, POUCH_ROLE_DEVICE, nonce);

    LOG_INF("Session key: %d", session->key);

    struct pouch_bufview plaintext;
    pouch_bufview_init(&plaintext, block);

    size_t plaintext_len = pouch_bufview_read_be16(&plaintext);
    if (plaintext_len != pouch_bufview_available(&plaintext))
    {
        LOG_ERR("Invalid plaintext length: %u", plaintext_len);
        buf_free(encrypted);
        return NULL;
    }

    size_t encrypted_len = plaintext_len + AUTH_TAG_LEN;
    block_size_write(encrypted, encrypted_len);
    uint8_t *ciphertext = buf_claim(encrypted, encrypted_len);
    size_t ciphertext_len;

    psa_status_t status =
        psa_aead_encrypt(session->key,
                         session->algorithm,
                         nonce,
                         sizeof(nonce),
                         session->pouch.ad,
                         session->pouch.block_index > 0 ? sizeof(session->pouch.ad) : 0,
                         pouch_bufview_read(&plaintext, plaintext_len),
                         plaintext_len,
                         ciphertext,
                         encrypted_len,
                         &ciphertext_len);
    if (status != PSA_SUCCESS)
    {
        LOG_ERR("Couldn't encrypt: %d", status);
        buf_free(encrypted);
        return NULL;
    }

    if (ciphertext_len != encrypted_len)
    {
        LOG_ERR("Unexpected length");
        buf_free(encrypted);
        return NULL;
    }

    // prepare for the next block:
    memcpy(&session->pouch.ad, &ciphertext[plaintext_len], AUTH_TAG_LEN);
    session->pouch.block_index++;

    return encrypted;
}

struct pouch_buf *session_decrypt_block(struct session *session, struct pouch_buf *block)
{
    struct pouch_buf *decrypted = buf_alloc(MAX_BLOCK_SIZE);
    if (decrypted == NULL)
    {
        return NULL;
    }

    uint8_t nonce[NONCE_LEN];
    nonce_generate(session, POUCH_ROLE_DEVICE, nonce);

    struct pouch_bufview ciphertext;
    pouch_bufview_init(&ciphertext, block);

    size_t ciphertext_len = pouch_bufview_read_be16(&ciphertext);
    if (ciphertext_len <= AUTH_TAG_LEN || ciphertext_len != pouch_bufview_available(&ciphertext))
    {
        LOG_ERR("Invalid ciphertext length: %u", ciphertext_len);
        buf_free(decrypted);
        return NULL;
    }

    size_t payload_len = ciphertext_len - AUTH_TAG_LEN;

    block_size_write(decrypted, payload_len);

    size_t plaintext_len;

    psa_status_t status =
        psa_aead_decrypt(session->key,
                         session->algorithm,
                         nonce,
                         sizeof(nonce),
                         session->pouch.ad,
                         session->pouch.block_index > 0 ? sizeof(session->pouch.ad) : 0,
                         pouch_bufview_read(&ciphertext, payload_len),
                         ciphertext_len,
                         buf_claim(decrypted, payload_len),
                         payload_len,
                         &plaintext_len);
    if (status != PSA_SUCCESS)
    {
        LOG_ERR("Failed decryption: %d", status);
        buf_free(decrypted);
        return NULL;
    }

    if (plaintext_len != payload_len)
    {
        LOG_ERR("Unexpected length");
        buf_free(decrypted);
        return NULL;
    }

    // prepare for the next block:
    pouch_bufview_memcpy(&ciphertext, &session->pouch.ad, AUTH_TAG_LEN);
    session->pouch.block_index++;

    atomic_set_bit(&session->flags, SESSION_VALID);

    return decrypted;
}
