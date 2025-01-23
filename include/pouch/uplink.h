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
 */
int pouch_uplink_entry_write(const char *path,
                             enum pouch_content_type content_type,
                             const void *data,
                             size_t len,
                             k_timeout_t timeout);

/**
 * Get the number of bytes pending in the uplink buffer.
 */
size_t pouch_uplink_pending(void);
