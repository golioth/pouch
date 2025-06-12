#pragma once


#include "session.h"
#include "../buf.h"

int downlink_session_start(const session_id_t *id,
                           psa_key_id_t private_key,
                           psa_algorithm_t algorithm);
void downlink_session_end(void);
int downlink_pouch_start(pouch_id_t id);
struct pouch_buf *downlink_block_decrypt(struct pouch_buf *block);
