#pragma once

#include "../pouch.h"
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <psa/crypto.h>


#define SESSION_ID_LEN 16
#define SESSION_ID_TAG_LEN (SESSION_ID_LEN - sizeof(uint64_t))
#define AUTH_TAG_LEN 16
#define AD_LEN AUTH_TAG_LEN

struct session;

enum session_id_type
{
    SESSION_ID_TYPE_SEQUENTIAL,
    SESSION_ID_TYPE_RANDOM,
};

typedef struct
{
    enum session_id_type type;
    enum pouch_role initiator;
    union
    {
        struct
        {
            uint8_t tag[SESSION_ID_TAG_LEN];
            uint64_t seqnum;
        } sequential;
        uint8_t random[SESSION_ID_LEN];
        uint8_t raw[SESSION_ID_LEN];
    };
} session_id_t;

enum session_flags
{
    SESSION_VALID,
    SESSION_ACTIVE,
    SESSION_HAS_POUCH,
};

struct session
{
    session_id_t id;
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

static inline bool session_id_is_equal(const session_id_t *a, const session_id_t *b)
{
    return a->type == b->type && a->initiator == b->initiator
        && memcmp(a->raw, b->raw, sizeof(a->raw)) == 0;
}

void session_init(struct session *session);
int session_key_generate(struct session *session, psa_key_id_t private_key, psa_key_usage_t usage);
void session_end(struct session *session);
struct pouch_buf *session_encrypt_block(struct session *session, struct pouch_buf *block);
struct pouch_buf *session_decrypt_block(struct session *session, struct pouch_buf *block);
