/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifdef __cplusplus
extern "C"
{
#endif

#pragma once

#include <psa/crypto_types.h>

#include <pouch/types.h>

/** Pouch configuration */
struct pouch_config
{
#if CONFIG_POUCH_ENCRYPTION_SAEAD
    /**
     * The device certificate in DER format.
     *
     * The memory pointed to by this field must remain valid while the pouch stack is in use.
     *
     * The certificate must be a valid X.509 certificate.
     * The certificate must be signed by a trusted CA.
     * The certificate must contain the public key that corresponds to the provided private key.
     */
    struct pouch_cert certificate;

    /**
     * The ID of the device's private key in the PSA key store.
     */
    psa_key_id_t private_key;
#elif CONFIG_POUCH_ENCRYPTION_NONE
    /**
     * The device ID. The length must not exceed @ref POUCH_DEVICE_ID_MAX_LEN.
     *
     * The memory pointed to by this field must remain valid while the pouch stack is in use.
     */
    const char *device_id;
#endif
};

/**
 * Initialize Pouch.
 *
 * @param config The configuration to use.
 */
int pouch_init(const struct pouch_config *config);

#ifdef __cplusplus
}
#endif
