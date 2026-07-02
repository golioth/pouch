/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

/* Zephyr's MbedTLS configuration mechanism does not provide sufficient control over the available
 * options for us to use it for pouch. To get around this, this file is included as a user config
 * file, which gets included in the MbedTLS configuration mechanism.
 *
 * Only PSA_BUILTIN_/PSA_ACCEL_ defines may be set here. Library-level MBEDTLS_* symbols must come
 * from Kconfig in Mbed TLS 4.0; see pouch/port/zephyr/Kconfig.
 */

/* Server certificates are signed with secp384r1 or ecdsa-with-SHA256.
 * We need to enable these to be able to parse the ASN.1 tag in the certificate, even if we don't
 * verify the certificate.
 */
#define MBEDTLS_PSA_BUILTIN_HASH
#define MBEDTLS_PSA_BUILTIN_ALG_SHA_256 1
#define MBEDTLS_PSA_BUILTIN_ALG_SHA_384 1
