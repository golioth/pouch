/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stddef.h>
#include <zephyr/net/tls_credentials.h>

int pouch_http_client_init(sec_tag_t sec_tag);
int pouch_http_client_sync(void);
