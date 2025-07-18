/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "cddl/header_types.h"
#include "buf.h"
#include <pouch/types.h>

/** Initialize crypto module */
int crypto_init(const struct pouch_config *config);

/** Notify the crypto module that a new pouch session is starting */
int crypto_session_start(void);

/** Notify the crypto module that the pouch session is ending */
void crypto_session_end(void);

/** Start a new downlink pouch */
int crypto_downlink_start(const struct encryption_info *encryption_info);

/** Initialize a new pouch in the encryption engine. */
int crypto_pouch_start(void);

/** Construct the encryption info part of the pouch header */
int crypto_header_get(struct encryption_info *encryption_info);

/**
 * Decrypt a block of data.
 */
struct pouch_buf *crypto_decrypt_block(struct pouch_buf *block);

/**
 * Encrypt a block of data.
 */
struct pouch_buf *crypto_encrypt_block(struct pouch_buf *block);
