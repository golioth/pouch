#pragma once
#include "session.h"
#include "../buf.h"
#include "cddl/header_types.h"

/** Start an uplink session */
int uplink_session_start(psa_algorithm_t algorithm, psa_key_id_t private_key);

/** End the ongoing uplink session */
void uplink_session_end(void);

/** Get the session info associated with the ongoing uplink session */
int uplink_header_get(struct saead_info *info);

/** Start a new pouch in the ongoing uplink session, and allocate a unique pouch ID for it. */
int uplink_pouch_start(void);

/** Encrypt a block in the current uplink pouch */
struct pouch_buf *uplink_encrypt_block(struct pouch_buf *block);
