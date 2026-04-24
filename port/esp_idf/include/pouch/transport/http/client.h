/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <pouch/pouch.h>

struct mtls_credentials
{
    const char *cert_pem;
    size_t cert_pem_len;
    const char *client_cert_pem;
    size_t client_cert_pem_len;
    const char *client_cert_der;
    size_t client_cert_der_len;
    const char *client_key_pem;
    size_t client_key_pem_len;
    const char *client_key_der;
    size_t client_key_der_len;
};

void http_client_transport_init(struct mtls_credentials *mtls_creds);
int http_client_transport_sync(void);
