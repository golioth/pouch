/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * @file device.h
 * @brief Pouch Serial transport - device side.
 *
 * The device side is a singleton module: there is no per-instance struct
 * exposed to the transport adapter. The adapter calls the module-level
 * @ref pouch_serial_device_recv function to deliver received frames and
 * implements the @ref pouch_serial_device_adapter vtable to transmit frames
 * and optionally signal the broker when data is available.
 */

typedef void (*pouch_serial_device_ready_cb_t)(void);

/**
 * Initialize the serial device transport.
 *
 * Must be called once before any frames are delivered via @ref pouch_serial_device_recv.
 *
 * @param ready_cb Optional callback to signal when the device has data available to send.
 *
 * @return 0 on success, negative error code on failure.
 */
void pouch_serial_device_init(pouch_serial_device_ready_cb_t ready_cb);

/**
 * Deliver a received frame from the broker to the device transport layer.
 *
 * Called by the transport adapter with each complete frame received from the
 * broker. The first byte of @p frame is the encoded header; the remaining
 * bytes are the payload.
 *
 * @param frame  Frame bytes, starting with the 1-byte header.
 * @param len    Total frame length in bytes (header + payload).
 * @return 0 on success, negative error code on failure.
 */
int pouch_serial_device_recv(const void *frame, size_t len);

/**
 * Get a frame of data to send over the serial link.
 *
 * Called by the transport adapter when it needs to send data.
 *
 * @param buf      Buffer that should be filled with data to send.
 * @param maxlen   Maximum number of bytes to send in the buffer. The device transport
 *                 layer will not write more than this many bytes to the buffer.
 *
 * @return Number of bytes written to the buffer.
 */
size_t pouch_serial_device_frame_get(uint8_t *buf, size_t maxlen);
