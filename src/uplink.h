/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>

/** Initialize the pouch uplink handler */
int uplink_init(void);

/** Enqueue encrypted data to be sent to the cloud */
int uplink_enqueue(const uint8_t *data, size_t len, k_timeout_t timeout);

/** Reset the uplink buffer */
int uplink_reset(void);

/** Get the number of bytes pending in the uplink buffer */
size_t uplink_pending(void);
