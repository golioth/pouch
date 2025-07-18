/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <pouch/types.h>

/**
 * Initialize Pouch.
 *
 * @param config The configuration to use.
 */
int pouch_init(const struct pouch_config *config);
