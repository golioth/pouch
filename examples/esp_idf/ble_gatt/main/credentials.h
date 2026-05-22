/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <pouch/pouch.h>

/**
 * Fill the pouch config with device certificate and private key.
 *
 * The certificate and key are embedded at build time from DER files
 * in the project directory.
 *
 * @param config Pouch configuration to populate.
 * @return 0 on success, negative on failure.
 */
int credentials_init(struct pouch_config *config);
