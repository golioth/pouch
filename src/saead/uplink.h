#pragma once
#include "session.h"
#include "../buf.h"
#include "cddl/session_info_types.h"

bool uplink_session_matches(const session_id_t *id, psa_algorithm_t alg);
int uplink_session_start(psa_algorithm_t algorithm, psa_key_id_t private_key);
void uplink_session_end(void);
int uplink_session_key_copy(psa_key_id_t *key, psa_key_usage_t usage);
int uplink_session_info_get(struct session_info *info);

pouch_id_t uplink_pouch_start(void);
struct pouch_buf *uplink_encrypt_block(struct pouch_buf *block);
