/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include <psa/crypto.h>
#include <pouch/types.h>

/**
 * Import the device private key into PSA
 *
 * @returns The assigned PSA key ID for the device's private key, or @c PSA_KEY_ID_NULL if the
 * private key couldn't be loaded.
 */
psa_key_id_t load_private_key(void);

/**
 * Load the device certificate.
 */
int load_certificate(struct pouch_cert *cert);

/** Free the device certificate. */
void free_certificate(struct pouch_cert *cert);
