/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "downlink.h"
#include "uplink.h"
#include "session.h"
#include "../cert.h"
#include <stdint.h>
#include <psa/crypto.h>
#include <zephyr/sys/byteorder.h>
#include <mbedtls/base64.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(saead_downlink, LOG_LEVEL_DBG);

#define DOWNLINK_KEY_USAGE (PSA_KEY_USAGE_DECRYPT | PSA_KEY_USAGE_VERIFY_MESSAGE)

static struct session downlink;
static struct
{
    /**
     * Highest sequence number we've seen the server create a session with.
     * If replay protection isn't enabled, we can still use this to ensure that the server hasn't
     * sent the same message since our last power cycle. This isn't proper replay protection, as
     * it'll get wiped when this device power cycles, but it's a cheap guard against low effort
     * replay attacks.
     */
    uint64_t seqnum;
    /**
     * Highest pouch ID we've seen the server send in the current session. Is only updated once
     * at least one block of the pouch is decrypted.
     */
    uint16_t pouch_id;
} server;

/** Check that this session is a valid follow up to the previous downlink session */
static bool is_valid_downlink(const struct session_id *id, psa_algorithm_t algorithm)
{
    if (!atomic_test_bit(&downlink.flags, SESSION_VALID))
    {
        // No previous session to invalidate the incoming session
        return true;
    }

    if (session_id_is_equal(&downlink.id, id) && downlink.algorithm != algorithm)
    {
        // Session ID is unchanged, parameters must be identical
        LOG_ERR("Algorithm doesn't match");
        return false;
    }

    if (id->initiator == POUCH_ROLE_SERVER)
    {
        // This was initiated by the server. If it's sequential, we can validate the sequence
        // number.
        if (id->type == SESSION_ID_TYPE_SEQUENTIAL && id->value.sequential.seqnum <= server.seqnum)
        {
            LOG_ERR("Old seqnum: %llu (was %llu)", id->value.sequential.seqnum, server.seqnum);
            return false;
        }
    }

    return true;
}

int saead_downlink_session_start(const struct session_id *id,
                                 psa_algorithm_t algorithm,
                                 uint8_t max_block_size_log,
                                 psa_key_id_t private_key)
{
    psa_key_id_t session_key;

    if (!is_valid_downlink(id, algorithm))
    {
        LOG_ERR("Invalid downlink");
        return -EBADMSG;
    }

    if (id->initiator == POUCH_ROLE_DEVICE)
    {
        if (id->type != SESSION_ID_TYPE_SEQUENTIAL)
        {
            // The server can only use our session if it's a sequential session ID
            LOG_ERR("Session reuse failed: ID not sequential");
            return -EBADMSG;
        }

        if (!saead_uplink_session_matches(id, max_block_size_log, algorithm))
        {
            // The server claims to use our uplink's session, but it doesn't match
            LOG_ERR("Session reuse failed: No match");
            return -EBADMSG;
        }

        // We can make a copy of the uplink session's key instead of deriving it again:
        session_key = saead_uplink_session_key_copy(DOWNLINK_KEY_USAGE);
    }
    else
    {
        // This isn't the uplink session, so we need to generate a new key
        struct pubkey pubkey;
        cert_server_key_get(&pubkey);

        session_key = session_key_generate(id,
                                           algorithm,
                                           max_block_size_log,
                                           private_key,
                                           &pubkey,
                                           DOWNLINK_KEY_USAGE);
    }

    if (session_key == PSA_KEY_ID_NULL)
    {
        LOG_ERR("Key generation failed");
        return -EIO;
    }

    downlink.flags = ATOMIC_INIT(0);
    downlink.pouch.id = 0;
    downlink.algorithm = algorithm;
    downlink.key = session_key;
    downlink.id = *id;

    atomic_set_bit(&downlink.flags, SESSION_ACTIVE);

    return 0;
}

void saead_downlink_session_end(void)
{
    session_end(&downlink);
}

int saead_downlink_pouch_start(pouch_id_t id)
{
    if (atomic_test_bit(&downlink.flags, SESSION_HAS_POUCH) && id <= server.pouch_id)
    {
        LOG_ERR("Replaying pouch %u (highest: %u)", id, server.pouch_id);
        return -EBADMSG;
    }

    return session_pouch_start(&downlink, id);
}

struct pouch_buf *saead_downlink_block_decrypt(struct pouch_buf *block)
{
    struct pouch_buf *decrypted = session_decrypt_block(&downlink, block);
    buf_free(block);
    if (decrypted == NULL)
    {
        return NULL;
    }

    // As we were able to decrypt a block, we know it's a legitimate session.
    atomic_set_bit(&downlink.flags, SESSION_VALID);
    atomic_set_bit(&downlink.flags, SESSION_HAS_POUCH);
    // We can also update our replay protection:
    server.pouch_id = downlink.pouch.id;
    if (downlink.id.initiator == POUCH_ROLE_SERVER
        && downlink.id.type == SESSION_ID_TYPE_SEQUENTIAL)
    {
        server.seqnum = downlink.id.value.sequential.seqnum;
    }

    return decrypted;
}
