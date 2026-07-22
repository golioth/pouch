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

/**
 * Run a function synchronously on the internal gateway work queue.
 *
 * Submits @p fn to the gateway work queue and blocks the calling thread until it has finished
 * executing. This moves the stack usage of blocking cloud operations (e.g. a DTLS handshake) off
 * the caller's thread while keeping the caller's control flow synchronous.
 *
 * Must not be called from the gateway work queue's own worker thread, or it would deadlock.
 *
 * @param fn  Function to execute on the work queue.
 * @param arg Argument passed to @p fn.
 */
void pouch_gateway_workq_run_sync(void (*fn)(void *arg), void *arg);
