/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "buf.h"
#include <pouch/types.h>

/** Initialize the pouch uplink handler */
void uplink_init(void);

void uplink_enqueue(struct pouch_buf *block);

/** Get the current uplink session ID */
uint32_t uplink_session_id(void);
