/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stddef.h>

/** mTLS credentials for the ESP-IDF HTTP client transport. */
struct mtls_credentials
{
    /** Server CA certificate in PEM format */
    const char *cert_pem;
    /** Length of @ref cert_pem in bytes. */
    size_t cert_pem_len;

    /** Device certificate in DER format */
    const char *client_cert_der;
    /** Length of @ref client_cert_der in bytes. */
    size_t client_cert_der_len;

    /** Device private key in DER format */
    const char *client_key_der;
    /** Length of @ref client_key_der in bytes. */
    size_t client_key_der_len;
};

/**
 * Initialize the HTTP client transport with mTLS credentials.
 *
 * Must be called once before http_client_transport_sync().  The pointer is
 * retained; the struct must remain valid for the transport's lifetime.
 *
 * @param mtls_creds Pointer to populated mTLS credentials struct.
 */
void http_client_transport_init(struct mtls_credentials *mtls_creds);

/**
 * Perform a full Pouch synchronization cycle over HTTP.
 *
 * Downloads the server certificate, uploads the device certificate, sends
 * any pending uplink data, and receives downlink data.  Blocks until the
 * sync completes or fails.
 *
 * @retval 0        Success.
 * @retval -EINVAL  Transport not initialized (call init first).
 * @retval -EACCES  Sync already in progress.
 * @retval -ENOENT  HTTP client initialization failed.
 * @retval -EIO     Server returned a non-2xx status code.
 * @return          Other negative errno on transport errors.
 */
int http_client_transport_sync(void);
