/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "buf.h"
#include "cddl/header_types.h"
#include <sys/types.h>

/**
 * Allocate and encode a pouch header.
 */
struct pouch_buf *pouch_header_create(void);

/**
 * Parse an incoming pouch header.
 *
 * Note: This does not move the bufview offset, regardless of the outcome.
 *
 * @returns a positive value indicating the length of a successfully parsed header
 * @returns 0 if the header couldn't be parsed, but there's no clear error
 * @returns -ENOTSUP if the header format is unsupported.
 * @returns -EIO if the header is invalid.
 */
ssize_t pouch_parse_header(const struct pouch_bufview *v, struct pouch_header *header);
