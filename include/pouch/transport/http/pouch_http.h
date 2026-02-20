/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stddef.h>
#include <zephyr/net/tls_credentials.h>

int pouch_http_init(const sec_tag_t *sec_tag_list, size_t sec_tag_count);
int pouch_http_sync(void);
