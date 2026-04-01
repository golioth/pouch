/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <pouch/port.h>

struct pouch_buf *blockbuf_alloc(pouch_timeout_t timeout);
void blockbuf_free(struct pouch_buf *buf);
