/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#pragma once

#include "session.h"
#include "../buf.h"

int saead_downlink_session_start(const struct session_id *id,
                                 psa_algorithm_t algorithm,
                                 uint8_t max_block_size_log,
                                 psa_key_id_t private_key);
void saead_downlink_session_end(void);
int saead_downlink_pouch_start(pouch_id_t id);
struct pouch_buf *saead_downlink_block_decrypt(struct pouch_buf *block);
