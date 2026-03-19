/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <zephyr/kernel.h>

/** Initialize the pouch downlink handler */
int downlink_init(struct k_work_q *pouch_work_queue);
