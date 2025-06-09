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

/** Encryption scheme for pouches */
enum pouch_encryption
{
    /** No encryption */
    POUCH_ENCRYPTION_PLAINTEXT,
};

/** Pouch configuration for plaintext encryption */
struct pouch_encryption_config_plaintext
{
    /**
     * The device ID. The length must not exceed @ref POUCH_DEVICE_ID_MAX_LEN.
     *
     * The memory pointed to by this field must remain valid while the pouch stack is in use.
     */
    const char *device_id;
};

/** Pouch configuration */
struct pouch_config
{
    /** The encryption mode to use */
    enum pouch_encryption encryption_type;
    /** Encryption configuration */
    union
    {
        /** Plaintext configuration */
        struct pouch_encryption_config_plaintext plaintext;
    } encryption;
};
