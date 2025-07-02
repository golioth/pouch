/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#pragma once

#include "../pouch.h"
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <psa/crypto.h>

#define SESSION_ID_LEN 16
#define SESSION_ID_TAG_LEN (SESSION_ID_LEN - sizeof(uint64_t))
#define AUTH_TAG_LEN 16
#define AD_LEN AUTH_TAG_LEN

/** The format of the session ID */
enum session_id_type
{
    /** Sequential session ID, allows replay protection */
    SESSION_ID_TYPE_SEQUENTIAL,
    /** Random session ID, does not allow replay protection */
    SESSION_ID_TYPE_RANDOM,
};

/** Session ID */
struct session_id
{
    enum session_id_type type;
    /** Which device initiated the session this identifies. */
    enum pouch_role initiator;
    union
    {
        struct
        {
            /** Randomized session tag */
            uint8_t tag[SESSION_ID_TAG_LEN];
            /** Sequence number for this session */
            uint64_t seqnum;
        } sequential;
        /** Session random ID */
        uint8_t random[SESSION_ID_LEN];
    } value;
};

enum session_flags
{
    SESSION_VALID,
    SESSION_ACTIVE,
    SESSION_HAS_POUCH,
};

struct session
{
    struct session_id id;
    atomic_t flags;
    psa_algorithm_t algorithm;
    psa_key_id_t key;
    struct
    {
        pouch_id_t id;
        uint32_t block_index;
        uint8_t ad[AD_LEN];
    } pouch;
};

static inline bool session_id_is_equal(const struct session_id *a, const struct session_id *b)
{
    return a->type == b->type && a->initiator == b->initiator
        && memcmp(&a->value, &b->value, sizeof(a->value)) == 0;
}

/** Generate a new session ID for the given session. */
int session_id_generate(struct session_id *id);

/** Generate a session key for the given session */
psa_key_id_t session_key_generate(const struct session_id *id,
                                  psa_algorithm_t algorithm,
                                  uint8_t max_block_size_log,
                                  psa_key_id_t private_key,
                                  const struct pubkey *pubkey,
                                  psa_key_usage_t usage);

/** End the given session, deleting the session key. */
void session_end(struct session *session);

int session_pouch_start(struct session *session, pouch_id_t pouch_id);

/** Encrypt the next block in the given session */
struct pouch_buf *session_encrypt_block(struct session *session, struct pouch_buf *block);

/** Decrypt the next block in the given session */
struct pouch_buf *session_decrypt_block(struct session *session, struct pouch_buf *block);
