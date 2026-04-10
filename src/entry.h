/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "buf.h"

void pouch_downlink_block_push(struct pouch_buf *pouch_buf);
int entry_block_close(int32_t timeout_ms);
