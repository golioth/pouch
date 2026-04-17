/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stddef.h>

struct mtls_credentials
{
    const char *cert_pem;
    size_t cert_len;
    const char *client_cert_pem;
    size_t client_cert_len;
    const char *client_key_pem;
    size_t client_key_len;
};
