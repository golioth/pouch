/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <pouch/port.h>

/** Initialize the pouch downlink handler */
int downlink_init(pouch_work_q_t *pouch_work_queue);

/**
 * Block until all in-flight downlink decryption has completed.
 *
 * Must be called before the downlink crypto session is torn down, and never
 * from the downlink work-queue thread.
 */
void pouch_downlink_flush(void);
