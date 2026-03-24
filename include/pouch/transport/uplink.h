/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include "types.h"

/**
 * @file uplink.h
 * @brief Uplink API for Pouch Transport layer implementations
 */

/** Uplink structure */
struct pouch_uplink;

/** Start a new uplink session */
struct pouch_uplink *pouch_uplink_start(void);

/** Fill the uplink buffer */
enum pouch_result pouch_uplink_fill(struct pouch_uplink *uplink, uint8_t *dst, size_t *dst_len);

/** Get the error status of the uplink */
int pouch_uplink_error(struct pouch_uplink *uplink);

/** Finish the uplink session */
void pouch_uplink_finish(struct pouch_uplink *uplink);
