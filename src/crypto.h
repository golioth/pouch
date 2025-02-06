/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#pragma once

#include "buf.h"
#include "cddl/header_encode_types.h"

/** Initialize a new pouch in the encryption engine. */
int crypto_pouch_start(void);

/** Construct the encryption info part of the pouch header */
int crypto_header_get(struct encryption_info *encryption_info);

/**
 * Encrypt a block of data.
 */
struct pouch_buf *crypto_encrypt_block(struct pouch_buf *block);
