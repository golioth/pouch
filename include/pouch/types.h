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

#include <stddef.h>
#include <stdint.h>
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
        .buffer = _raw_cert_der,         \
        .size = sizeof(_raw_cert_der),   \
    }

/** Certificate definition */
struct pouch_cert
{
    /**
     * The certificate in DER or PEM format.
     */
    const uint8_t *buffer;
    /** The length of the certificate */
    size_t size;
};

#ifdef __cplusplus
}
#endif
