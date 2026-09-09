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

#if defined(CONFIG_EXAMPLE_FW_SIGNED_URL)
/**
 * Import the embedded device private key again, for signing.
 *
 * A PSA key permits one algorithm, and the key that derives the Pouch session
 * key cannot also sign, so signed-URL updates need their own import of the same
 * material.
 *
 * @return The assigned PSA key ID, or PSA_KEY_ID_NULL on failure.
 */
psa_key_id_t load_signing_key(void);
#endif

/**
 * Point @p cert at the embedded device certificate.
 *
 * @return 0 on success.
 */
int load_certificate(struct pouch_cert *cert);
