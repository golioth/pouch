/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <pouch/port.h>

/**
 * Submit a work item to the internal gateway work queue.
 *
 * This is used to separate blocking operations from the transport's receive thread so it continues
 * to process incoming traffic.
 *
 * @param work The work item to submit.
 */
void pouch_gateway_submit_close_work(pouch_work_t *work);
