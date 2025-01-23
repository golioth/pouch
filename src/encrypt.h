/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#pragma once

#include "block.h"

#include <stddef.h>
#include <stdint.h>

#include <zcbor_common.h>

/**
 * Encode the pouch header encryption info to the buffer.
 */
bool encrypt_pouch_header_encryption_info_encode(zcbor_state_t *zse);

/**
 * Encrypt a block of data.
 */
int encrypt_block(const struct block *block, k_timeout_t timeout);
