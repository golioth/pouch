/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include <pouch/pouch.h>

extern const uint8_t test_device_cert_ref[32];

const struct pouch_config *setup_credentials(void);
