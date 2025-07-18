/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include "session.h"
#include "../buf.h"
#include "cddl/header_types.h"

/** Start an uplink session */
int saead_uplink_session_start(psa_algorithm_t algorithm, psa_key_id_t private_key);

/** End the ongoing uplink session */
void saead_uplink_session_end(void);

/** Get the session info associated with the ongoing uplink session */
int saead_uplink_header_get(struct saead_info *info);

/** Start a new pouch in the ongoing uplink session, and allocate a unique pouch ID for it. */
int saead_uplink_pouch_start(void);

/** Encrypt a block in the current uplink pouch */
struct pouch_buf *saead_uplink_encrypt_block(struct pouch_buf *block);

/**
 * Get whether the given session ID, block size and algorithm matches the uplink's ongoing session
 * parameters.
 */
bool saead_uplink_session_matches(const struct session_id *id,
                                  uint8_t max_block_size_log,
                                  psa_algorithm_t algorithm);

/** Make a copy of the uplink session's session key. */
psa_key_id_t saead_uplink_session_key_copy(psa_key_usage_t usage);
