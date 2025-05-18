/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @file downlink.h
 * @brief Pouch downlink API for receiving data from the cloud
 */

/**
 * Callback executed with reassembled pouch downlink entry.
 *
 * The content type is defined by the CoAP Content-Formats sub-registry within the IANA CoRE.
 * See @ref content_types.
 *
 * @param path The path of the entry.
 * @param content_type The content type of the entry.
 * @param data The data (payload) of the entry.
 * @param len The length of the data.
 * @param offset The offset of the data (when data is split across multiple blocks).
 * @param is_last Defines whether this is the last data fragment (last block).
 */
void downlink_received(const char *path,
                       uint16_t content_type,
                       const void *data,
                       size_t len,
                       size_t offset,
                       bool is_last);
