/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <psa/crypto.h>

#include <pouch/types.h>

/**
 * Import the embedded device private key into PSA.
 *
 * @return The assigned PSA key ID, or PSA_KEY_ID_NULL on failure.
 */
psa_key_id_t load_private_key(void);

/**
 * Point @p cert at the embedded device certificate.
 *
 * @return 0 on success.
 */
int load_certificate(struct pouch_cert *cert);
