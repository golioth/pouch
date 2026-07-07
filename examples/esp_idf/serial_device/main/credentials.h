/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <pouch/pouch.h>

typedef int (*cred_set_fn_t)(const char *buf);

/**
 * Store device certificate (base64-encoded DER) to NVS.
 */
int cred_set_device_crt(const char *b64_der);

/**
 * Store device private key (base64-encoded DER) to NVS.
 */
int cred_set_device_key(const char *b64_der);

/**
 * Load credentials from NVS and fill the Pouch config.
 *
 * @return 0 on success, negative errno if credentials are missing or invalid.
 */
int credentials_init(struct pouch_config *config);
