/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

size_t hex2bin(const char *hex, size_t hexlen, uint8_t *buf, size_t buflen);
