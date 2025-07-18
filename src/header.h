/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "buf.h"

/**
 * Allocate and encode a pouch header.
 */
struct pouch_buf *pouch_header_create(void);
