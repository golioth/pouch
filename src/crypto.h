/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#pragma once

#include "cddl/header_encode_types.h"
#include "buf.h"
#include <pouch/types.h>

/** Initialize a new pouch in the encryption engine. */
int crypto_pouch_start(void);

/** Construct the encryption info part of the pouch header */
int crypto_header_get(const struct pouch_config *config, struct encryption_info *encryption_info);

/**
 * Encrypt a block of data.
 */
struct pouch_buf *crypto_encrypt_block(struct pouch_buf *block);
