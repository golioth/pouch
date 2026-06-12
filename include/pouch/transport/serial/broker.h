/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <pouch/port.h>

/**
 * @file broker.h
 * @brief Pouch Serial transport (broker side)
 *
 * The broker is the master of the serial exchange. It initiates all transfers,
 * drives the provisioning and data exchange sequence, and owns a
 * @ref pouch_serial_broker instance allocated by the transport adapter.
 *
 * Typical usage:
 *
 *   1. Embed @ref pouch_serial_broker inside your adapter context struct.
 *   2. Fill in a @ref pouch_serial_broker_adapter with your ready/end callbacks
 *      and the adapter's maximum frame size.
 *   3. Call @ref pouch_serial_broker_init once at startup.
 *   4. Call @ref pouch_serial_broker_start to begin an exchange with a device.
 *   5. Feed received frames to @ref pouch_serial_broker_recv.
 *   6. Call @ref pouch_serial_broker_notify when the device signals that it
 *      has data available (e.g. via an interrupt line).
 */

struct pouch_serial_broker;
struct pouch_gateway_node_info;

/**
 * Transport adapter interface for the serial broker.
 *
 * The adapter is responsible for all physical bus concerns (framing, length
 * delimitation, error detection). It presents complete frames to the broker
 * protocol layer and retrieves complete frames from it via
 * @ref pouch_serial_broker_frame_get.
 */
struct pouch_serial_broker_adapter
{
    /**
     * Signal to the adapter that the broker has a frame ready to send.
     *
     * Called whenever @ref pouch_serial_broker_frame_get would return a non-zero
     * length. The adapter should call @ref pouch_serial_broker_frame_get at its
     * next opportunity.
     *
     * @param broker Broker instance.
     */
    void (*ready)(const struct pouch_serial_broker *broker);

    /**
     * Called when the exchange sequence completes or fails.
     *
     * @param broker  Broker instance.
     * @param success true if the full exchange completed successfully.
     */
    void (*end)(const struct pouch_serial_broker *broker, bool success);
};

/**
 * Create a broker instance.
 * The broker must be started with @ref pouch_serial_broker_start before it can
 * process frames.
 *
 * @param adapter Serial adapter to use for this broker.
 * @return Pointer to the created broker instance, or NULL on failure.
 */
struct pouch_serial_broker *pouch_serial_broker_create(
    const struct pouch_serial_broker_adapter *adapter);

/**
 * Start the Pouch exchange sequence with a device.
 *
 * Drives the full sequence:
 *   Info -> Server Cert (if needed) -> Device Cert (if needed) ->
 *   Uplink drain -> Downlink delivery
 *
 * The adapter's @c end callback is invoked when the sequence completes or
 * fails. Only one exchange may be in progress at a time.
 *
 * @param broker  Broker instance.
 */
void pouch_serial_broker_start(struct pouch_serial_broker *broker);

/**
 * Deliver a received frame from the device to the broker transport layer.
 *
 * Called by the transport adapter with each complete frame received from the
 * device. The first byte of @p frame is the encoded header; the remaining
 * bytes are the payload.
 *
 * @param broker  Broker instance.
 * @param frame   Frame bytes, starting with the 1-byte header.
 * @param len     Total frame length in bytes (header + payload).
 * @return 0 on success, negative error code on failure.
 */
int pouch_serial_broker_recv(struct pouch_serial_broker *broker, const void *frame, size_t len);

/**
 * Get a frame of data to send over the serial link.
 *
 * Called by the transport adapter when it needs to send data to the device.
 * The broker fills @p buf with the next pending frame (header + payload).
 *
 * @param broker  Broker instance.
 * @param buf     Buffer that should be filled with the frame to send.
 * @param maxlen  Maximum number of bytes to write to @p buf. The broker will
 *                not write more than this many bytes.
 *
 * @return Number of bytes written to @p buf, or 0 if there is no frame ready.
 */
size_t pouch_serial_broker_frame_get(struct pouch_serial_broker *broker,
                                     uint8_t *buf,
                                     size_t maxlen);

/**
 * Signal to the broker that the device has data available to send.
 *
 * Called by the transport adapter when the device asserts an interrupt line or
 * otherwise indicates it has uplink data ready. The broker will issue a prompt
 * on the uplink channel at its next opportunity.
 *
 * @param broker  Broker instance.
 */
void pouch_serial_broker_notify(struct pouch_serial_broker *broker);

/**
 * Get the adapter associated with a broker instance.
 *
 * @param broker  Broker instance.
 * @return Pointer to the adapter passed to @ref pouch_serial_broker_create.
 */
const struct pouch_serial_broker_adapter *pouch_serial_broker_adapter_get(
    const struct pouch_serial_broker *broker);
