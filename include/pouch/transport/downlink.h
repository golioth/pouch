/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

/**
 * @file downlink.h
 * @brief Downlink API for Pouch Transport layer implementations
 */

/** Start a new downlink session */
void pouch_downlink_start(void);

/** Push downlink data into Pouch */
void pouch_downlink_push(const void *buf, size_t buf_len);

/** Finish the downlink session */
void pouch_downlink_finish(void);
