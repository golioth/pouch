#include "downlink.h"
#include "uplink.h"
#include "session.h"
#include <stdint.h>
#include <psa/crypto.h>
#include <zephyr/sys/byteorder.h>
#include <mbedtls/base64.h>

#define DOWNLINK_KEY_USAGE (PSA_KEY_USAGE_DECRYPT | PSA_KEY_USAGE_VERIFY_MESSAGE)

static struct session downlink;

static bool session_is_valid(const session_id_t *id, psa_algorithm_t algorithm)
{
    // Check that this session is a valid follow up to the previous downlink session
    if (atomic_test_bit(&downlink.flags, SESSION_VALID))
    {
        if (session_id_is_equal(&downlink.id, id))
        {
            // Session ID is identical, parameters must be identical
            if (downlink.algorithm != algorithm)
            {
                return false;
            }
        }
        else if (id->initiator == POUCH_ROLE_SERVER)
        {
            /* This was initiated by the server. If it's sequential, and the previous session was as
             * well, we need to see a higher sequence number.
             */
            if (id->type == SESSION_ID_TYPE_SEQUENTIAL
                && downlink.id.type == SESSION_ID_TYPE_SEQUENTIAL
                && id->sequential.seqnum <= downlink.id.sequential.seqnum)
            {
                return false;
            }
        }
    }

    // Responding in uplink session:
    if (id->initiator == POUCH_ROLE_DEVICE)
    {
        if (id->type != SESSION_ID_TYPE_SEQUENTIAL || !uplink_session_matches(id, algorithm))
        {
            // Illegal ID type, or not our current uplink session
            return false;
        }
    }

    return true;
}

int downlink_session_start(const session_id_t *id,
                           psa_key_id_t private_key,
                           psa_algorithm_t algorithm)
{
    int err;

    if (!session_is_valid(id, algorithm))
    {
        return -EBADMSG;
    }

    session_init(&downlink);
    downlink.algorithm = algorithm;
    downlink.id = *id;

    if (uplink_session_matches(id, algorithm))
    {
        err = uplink_session_key_copy(&downlink.key, DOWNLINK_KEY_USAGE);
    }
    else
    {
        err = session_key_generate(&downlink, private_key, DOWNLINK_KEY_USAGE);
    }

    if (err)
    {
        return err;
    }

    atomic_set_bit(&downlink.flags, SESSION_ACTIVE);

    return 0;
}

void downlink_session_end(void)
{
    session_end(&downlink);
}

int downlink_pouch_start(pouch_id_t id)
{
    if (atomic_test_and_set_bit(&downlink.flags, SESSION_HAS_POUCH) && id <= downlink.pouch.id)
    {
        return -EINVAL;
    }

    downlink.pouch.id = id;
    downlink.pouch.block_index = 0;

    return 0;
}

struct pouch_buf *downlink_block_decrypt(struct pouch_buf *block)
{
    return session_encrypt_block(&downlink, block);
}
