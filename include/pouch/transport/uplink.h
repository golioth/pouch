/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <pouch/port.h>
#include <stdint.h>
#include <stddef.h>
#include "types.h"

/**
 * @file uplink.h
 * @brief Uplink API for Pouch Transport layer implementations
 */

/**
 * Start a new uplink session.
 *
 * A session establishes a new session key, but does not open a pouch on its
 * own - call @ref pouch_uplink_pouch_open to open the first pouch. Multiple
 * pouches can be opened and closed in sequence within the same session, each
 * reusing the session's key.
 *
 * @return 0 on success or a negative error code on failure.
 */
int pouch_uplink_start(void);

/**
 * Open a new pouch within the current uplink session.
 *
 * Must be called after @ref pouch_uplink_start, and after any previously
 * opened pouch has been closed with @ref pouch_uplink_pouch_close.
 *
 * @return 0 on success or a negative error code on failure.
 */
int pouch_uplink_pouch_open(void);

/**
 * Close the currently open pouch, without ending the uplink session.
 *
 * Should only be called once the transport has drained all pouch data, i.e.
 * once @ref pouch_uplink_fill has returned @ref POUCH_NO_MORE_DATA. The
 * session remains active and ready for another @ref pouch_uplink_pouch_open
 * call, or to be ended with @ref pouch_uplink_finish.
 *
 * @return 0 on success or a negative error code on failure.
 */
int pouch_uplink_pouch_close(void);

/** Wait for a block to become available in the uplink queue */
int pouch_wait_for_queue(pouch_timeout_t timeout);

/** Fill the uplink buffer */
enum pouch_result pouch_uplink_fill(uint8_t *dst, size_t *dst_len);

/** Get the error status of the uplink */
int pouch_uplink_error(void);

/**
 * Finish the uplink session.
 *
 * If a pouch is still open, it is force-closed first (as if
 * @ref pouch_uplink_pouch_close had been called) to avoid leaking any
 * pending buffers.
 */
void pouch_uplink_finish(void);
