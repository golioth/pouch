/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#pragma once

#include "cddl/session_info_types.h"
#include "buf.h"
#include "pouch.h"
#include <pouch/types.h>

int crypto_init(const struct pouch_config *config);

/** Initialize a new pouch in the encryption engine. */
int crypto_session_start(void);
void crypto_session_end(void);
pouch_id_t crypto_pouch_start(void);

/** Construct the encryption info part of the pouch header */
int crypto_session_info_get(struct session_info *session_info);

/**
 * Encrypt a block of data.
 */
struct pouch_buf *crypto_encrypt_block(struct pouch_buf *block);
struct pouch_buf *crypto_decrypt_block(struct pouch_buf *block);
