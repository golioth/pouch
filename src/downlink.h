/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <pouch/port.h>
#include <zephyr/kernel.h>

/** Initialize the pouch downlink handler */
int downlink_init(pouch_work_q_t *pouch_work_queue);
