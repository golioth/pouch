/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <psa/crypto_types.h>

/**
 * @file types.h
 * @brief Pouch type definitions
 */

#define POUCH_VERSION 1

/**
 * @defgroup content_types Pouch content types
 * @{
 */

/** The content is a raw octet stream */
#define POUCH_CONTENT_TYPE_OCTET_STREAM 42
/** The content is a JSON encoded object */
#define POUCH_CONTENT_TYPE_JSON 50
/** The content is a CBOR encoded object */
#define POUCH_CONTENT_TYPE_CBOR 60

/** @} */

/** The maximum length of a device ID */
#define POUCH_DEVICE_ID_MAX_LEN 32

/** Create a pouch certificate from a raw byte array of a DER encoded x509 certificate */
#define POUCH_CERTIFICATE(_raw_cert_der) \
    {                                    \
        .der = _raw_cert_der,            \
        .size = sizeof(_raw_cert_der),   \
    }

/** Certificate definition */
struct pouch_cert
{
    /**
     * The certificate in DER format.
     */
    const uint8_t *der;
    /** The length of the certificate */
    size_t size;
};

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
