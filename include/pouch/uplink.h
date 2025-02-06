/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#pragma once

#include <pouch/types.h>

#include <zephyr/kernel.h>

/**
 * @file uplink.h
 * @brief Pouch uplink API for sending data to the cloud
 */

/**
 * Write an entry to the pouch uplink.
 *
 * @param path The CoAP path to write the entry to.
 * @param content_type The content type of the entry.
 * @param data The data to write.
 * @param len The length of the data.
 * @param timeout The timeout for the operation.
 */
int pouch_uplink_entry_write(const char *path,
                             uint16_t content_type,
                             const void *data,
                             size_t len,
                             k_timeout_t timeout);

/**
 * Close the current uplink session by finalizing the open pouch.
 */
int pouch_uplink_close(k_timeout_t timeout);
