/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <zephyr/sys_clock.h>

#include "buf.h"

void pouch_downlink_block_push(struct pouch_buf *pouch_buf);
int entry_block_close(k_timeout_t timeout);
