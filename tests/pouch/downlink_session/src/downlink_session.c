/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Tests for src/saead/downlink.c's session handling: receiving multiple
 * pouches within a single downlink session without needlessly regenerating
 * the session key, and rejecting session/pouch IDs that don't form a valid
 * continuation of a previous session.
 *
 * This links the saead downlink/session code directly, with real PSA
 * crypto (ECDH + AEAD), rather than going through the full pouch library
 * (CONFIG_POUCH_ENCRYPTION_MOCK bypasses the code under test entirely, and
 * the non-mock path needs a real device/server X.509 certificate chain
 * just to reach it). Its few dependencies outside the saead module -
 * src/cert.c and the uplink-session-reuse half of src/saead/uplink.c - are
 * replaced with test doubles below.
 */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <psa/crypto.h>

#include "block.h"
#include "buf.h"
#include "cert.h"
#include "pouch.h"
#include "saead/downlink.h"
#include "saead/session.h"
#include "saead/uplink.h"

#define TEST_ALGORITHM PSA_ALG_CHACHA20_POLY1305
#define TEST_BLOCK_SIZE_LOG 9
#define NONCE_LEN 12

/*
 * All sessions in this suite are server-initiated, so downlink.c's
 * uplink-session reuse path (for device-initiated sessions) is never
 * taken. These only need to exist to satisfy the linker.
 */
bool saead_uplink_session_matches(const struct session_id *id,
                                  uint8_t max_block_size_log,
                                  psa_algorithm_t algorithm)
{
    ARG_UNUSED(id);
    ARG_UNUSED(max_block_size_log);
    ARG_UNUSED(algorithm);
    return false;
}

psa_key_id_t saead_uplink_session_key_copy(psa_key_usage_t usage)
{
    ARG_UNUSED(usage);
    return PSA_KEY_ID_NULL;
}

/*
 * session.c's only dependency on block.c. block.c itself isn't linked in
 * here, as it drags in the blockbuf pool (and its Kconfig) for
 * block_alloc()/block_free(), neither of which is needed to exercise the
 * session code.
 */
void block_size_write(struct pouch_buf *block, uint16_t size)
{
    pouch_put_be16(size, buf_claim(block, sizeof(uint16_t)));
}

/*
 * Test double for cert_server_key_get(): stands in for a server
 * certificate the device would normally have already validated. Counting
 * calls lets tests prove the session key was (or wasn't) re-derived,
 * without relying on psa_key_id_t values happening to differ.
 */
static struct pubkey server_pubkey;
static int cert_server_key_get_calls;

void cert_server_key_get(struct pubkey *out)
{
    cert_server_key_get_calls++;
    *out = server_pubkey;
}

static psa_key_id_t device_privkey;
static psa_key_id_t server_privkey;
static struct pubkey device_pubkey;

static psa_key_id_t generate_p256_keypair(struct pubkey *pubkey_out)
{
    psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;

    psa_set_key_type(&attr, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attr, 256);
    psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_DERIVE);
    psa_set_key_algorithm(&attr, PSA_ALG_ECDH);

    psa_key_id_t key = PSA_KEY_ID_NULL;
    psa_status_t status = psa_generate_key(&attr, &key);
    zassert_equal(status, PSA_SUCCESS, "keypair generation failed: %d", (int) status);

    pubkey_out->len = 0;
    status =
        psa_export_public_key(key, pubkey_out->data, sizeof(pubkey_out->data), &pubkey_out->len);
    zassert_equal(status, PSA_SUCCESS, "public key export failed: %d", (int) status);

    return key;
}

static void *suite_setup(void)
{
    zassert_equal(psa_crypto_init(), PSA_SUCCESS, "psa_crypto_init failed");

    /* A device keypair (used as saead_downlink_session_start()'s
     * private_key) and a "server" keypair. cert_server_key_get() above
     * always hands back the server's public key, as if the device had
     * already received and validated the server's certificate. */
    device_privkey = generate_p256_keypair(&device_pubkey);
    server_privkey = generate_p256_keypair(&server_pubkey);

    return NULL;
}

ZTEST_SUITE(downlink_session, NULL, suite_setup, NULL, NULL, NULL);

/*
 * A minimal mirror of session_encrypt_block() (src/saead/session.c) for the
 * *server* side of a downlink session: same block framing, nonce format and
 * AD chaining, but with the sender role in the nonce set to
 * POUCH_ROLE_SERVER rather than the POUCH_ROLE_DEVICE that
 * session_encrypt_block() hardcodes (it's only ever used to encrypt the
 * device's own uplink). There's no gateway-side SAEAD implementation in
 * this repo to reuse instead - the real server that would build these
 * blocks lives in the cloud.
 */
struct fake_server_session
{
    psa_key_id_t key;
    psa_algorithm_t algorithm;
    uint16_t pouch_id;
    uint32_t block_index;
    uint8_t ad[AUTH_TAG_LEN];
};

static void fake_server_session_start(struct fake_server_session *s,
                                      psa_key_id_t key,
                                      psa_algorithm_t algorithm,
                                      uint16_t pouch_id)
{
    s->key = key;
    s->algorithm = algorithm;
    s->pouch_id = pouch_id;
    s->block_index = 0;
}

static struct pouch_buf *fake_server_encrypt_block(struct fake_server_session *s,
                                                   const uint8_t *plaintext,
                                                   size_t plaintext_len)
{
    size_t encrypted_len = plaintext_len + AUTH_TAG_LEN;
    struct pouch_buf *encrypted = buf_alloc(sizeof(uint16_t) + encrypted_len);

    zassert_not_null(encrypted, "buf_alloc failed");

    uint8_t nonce[NONCE_LEN];
    pouch_put_be16(s->pouch_id, &nonce[0]);
    pouch_put_be16(s->block_index, &nonce[2]);
    nonce[4] = POUCH_ROLE_SERVER;
    memset(&nonce[5], 0, sizeof(nonce) - 5);

    block_size_write(encrypted, encrypted_len);
    uint8_t *ciphertext = buf_claim(encrypted, encrypted_len);
    size_t ciphertext_len;

    psa_status_t status = psa_aead_encrypt(s->key,
                                           s->algorithm,
                                           nonce,
                                           sizeof(nonce),
                                           s->ad,
                                           s->block_index > 0 ? sizeof(s->ad) : 0,
                                           plaintext,
                                           plaintext_len,
                                           ciphertext,
                                           encrypted_len,
                                           &ciphertext_len);
    zassert_equal(status, PSA_SUCCESS, "encrypt failed: %d", (int) status);
    zassert_equal(ciphertext_len, encrypted_len, "unexpected ciphertext length");

    memcpy(s->ad, &ciphertext[plaintext_len], AUTH_TAG_LEN);
    s->block_index++;

    return encrypted;
}

static void make_session_id(struct session_id *id, uint64_t seqnum)
{
    memset(id, 0, sizeof(*id));
    id->type = SESSION_ID_TYPE_SEQUENTIAL;
    id->initiator = POUCH_ROLE_SERVER;
    memset(id->value.sequential.tag, 0xA5, sizeof(id->value.sequential.tag));
    id->value.sequential.seqnum = seqnum;
}

/*
 * Derive the session key the *server* side of the session would compute
 * for the given session ID, mirroring what saead_downlink_session_start()
 * derives on the device side. ECDH is symmetric, and both sides feed
 * identical id/algorithm/max_block_size_log info into the HKDF, so the two
 * derivations produce the same raw key material - each side just ends up
 * with its own psa_key_id_t for it, carrying the usage it actually needs.
 */
static psa_key_id_t derive_server_key(const struct session_id *id)
{
    psa_key_id_t key = session_key_generate(id,
                                            TEST_ALGORITHM,
                                            TEST_BLOCK_SIZE_LOG,
                                            server_privkey,
                                            &device_pubkey,
                                            PSA_KEY_USAGE_ENCRYPT);
    zassert_not_equal(key, PSA_KEY_ID_NULL, "server-side key derivation failed");
    return key;
}

/* Round-trip one single-block pouch through the device's downlink session
 * and check the decrypted payload matches. */
static void receive_pouch(pouch_id_t pouch_id,
                          struct fake_server_session *server,
                          const char *payload)
{
    int err = saead_downlink_pouch_start(pouch_id);
    zassert_ok(err, "pouch_start(%u) failed: %d", pouch_id, err);

    struct pouch_buf *encrypted =
        fake_server_encrypt_block(server, (const uint8_t *) payload, strlen(payload));

    struct pouch_buf *decrypted = saead_downlink_block_buf_alloc();
    zassert_not_null(decrypted, "block buf alloc failed");

    err = saead_downlink_block_decrypt(encrypted, decrypted);
    zassert_ok(err, "block decrypt failed: %d", err);

    struct pouch_bufview view;
    pouch_bufview_init(&view, decrypted);
    uint16_t len;
    zassert_ok(pouch_bufview_read_be16(&view, &len), "reading decrypted length failed");
    zassert_equal(len, strlen(payload), "decrypted length mismatch");
    zassert_mem_equal(pouch_bufview_read(&view, len), payload, len, "decrypted payload mismatch");

    buf_free(encrypted);
    buf_free(decrypted);
}

ZTEST(downlink_session, test_downlink_session_lifecycle)
{
    struct session_id id;
    struct fake_server_session server;
    int err;

    /*
     * --- Multiple pouches in a single downlink session ---
     *
     * crypto_downlink_start() calls saead_downlink_session_start() once per
     * *pouch*, not once per session: the session parameters are repeated in
     * every pouch's header. The first pouch establishes the session; later
     * pouches with the same session ID and parameters must reuse it rather
     * than re-deriving the key (an ECDH + HKDF operation) every time.
     */
    make_session_id(&id, 1);
    cert_server_key_get_calls = 0;

    err = saead_downlink_session_start(&id, TEST_ALGORITHM, TEST_BLOCK_SIZE_LOG, device_privkey);
    zassert_ok(err, "session_start failed: %d", err);
    zassert_equal(cert_server_key_get_calls, 1, "expected exactly one key derivation");

    psa_key_id_t server_key = derive_server_key(&id);
    fake_server_session_start(&server, server_key, TEST_ALGORITHM, 1);
    receive_pouch(1, &server, "hello from pouch one");

    /* Second pouch, same session: its header repeats the same session ID
     * and parameters. */
    err = saead_downlink_session_start(&id, TEST_ALGORITHM, TEST_BLOCK_SIZE_LOG, device_privkey);
    zassert_ok(err, "session_start (2nd pouch) failed: %d", err);
    zassert_equal(cert_server_key_get_calls,
                  1,
                  "session key must not be regenerated while the session is active");

    fake_server_session_start(&server, server_key, TEST_ALGORITHM, 2);
    receive_pouch(2, &server, "hello from pouch two, a bit longer");

    /*
     * --- Session parameters are validated ---
     *
     * Once the session has decrypted at least one block it's "valid", and
     * the same session ID can no longer be reused with different
     * parameters.
     */
    err =
        saead_downlink_session_start(&id, TEST_ALGORITHM, TEST_BLOCK_SIZE_LOG + 1, device_privkey);
    zassert_equal(err, -EBADMSG, "changed max_block_size_log should be rejected, got %d", err);

    err = saead_downlink_session_start(&id, PSA_ALG_GCM, TEST_BLOCK_SIZE_LOG, device_privkey);
    zassert_equal(err, -EBADMSG, "changed algorithm should be rejected, got %d", err);

    /* The rejected attempts must not have disturbed the still-active
     * session, nor caused a key re-derivation: it resumes normally. */
    zassert_equal(cert_server_key_get_calls, 1, "rejected attempts must not derive a new key");
    err = saead_downlink_session_start(&id, TEST_ALGORITHM, TEST_BLOCK_SIZE_LOG, device_privkey);
    zassert_ok(err, "resuming the original session failed: %d", err);

    fake_server_session_start(&server, server_key, TEST_ALGORITHM, 3);
    receive_pouch(3, &server, "still the same session");

    saead_downlink_session_end();

    /*
     * --- Non-increasing session IDs are rejected ---
     *
     * A new sequential session must have a strictly higher sequence number
     * than any session the server has previously started with the device,
     * even across a session boundary (this device-wide high-water mark is
     * the replay guard for sessions the device didn't itself initiate).
     */
    struct session_id replay_id;

    make_session_id(&replay_id, 1);
    err = saead_downlink_session_start(&replay_id,
                                       TEST_ALGORITHM,
                                       TEST_BLOCK_SIZE_LOG,
                                       device_privkey);
    zassert_equal(err, -EBADMSG, "reused seqnum should be rejected, got %d", err);

    make_session_id(&replay_id, 0);
    err = saead_downlink_session_start(&replay_id,
                                       TEST_ALGORITHM,
                                       TEST_BLOCK_SIZE_LOG,
                                       device_privkey);
    zassert_equal(err, -EBADMSG, "decreasing seqnum should be rejected, got %d", err);

    make_session_id(&id, 2);
    err = saead_downlink_session_start(&id, TEST_ALGORITHM, TEST_BLOCK_SIZE_LOG, device_privkey);
    zassert_ok(err, "increasing seqnum should be accepted: %d", err);

    server_key = derive_server_key(&id);
    fake_server_session_start(&server, server_key, TEST_ALGORITHM, 4);
    receive_pouch(4, &server, "first pouch of the new session");

    /*
     * --- Non-increasing pouch IDs are rejected ---
     *
     * Within a session (and across sessions, since the guard is a
     * device-wide high-water mark), pouch IDs must keep increasing.
     */
    err = saead_downlink_pouch_start(4);
    zassert_equal(err, -EBADMSG, "replaying the current pouch ID should be rejected, got %d", err);

    err = saead_downlink_pouch_start(2);
    zassert_equal(err, -EBADMSG, "replaying an older pouch ID should be rejected, got %d", err);

    /* A third pouch in this same session: same key derivation guard as
     * before applies. */
    err = saead_downlink_session_start(&id, TEST_ALGORITHM, TEST_BLOCK_SIZE_LOG, device_privkey);
    zassert_ok(err, "session_start (3rd pouch) failed: %d", err);
    zassert_equal(cert_server_key_get_calls, 2, "session key must not be regenerated again");

    fake_server_session_start(&server, server_key, TEST_ALGORITHM, 5);
    receive_pouch(5, &server, "a later pouch in the new session");

    saead_downlink_session_end();
}
