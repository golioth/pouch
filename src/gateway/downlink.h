/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct pouch_gateway_downlink_context;
typedef void (*pouch_gateway_downlink_data_available_cb)(void *);

/**
 * Initialize a downlink context.
 *
 * The caller holds the consumer reference until it calls pouch_gateway_downlink_close().
 * Consumer calls on the context must not overlap.
 * @p data_available_cb and @p arg must stay valid until pouch_gateway_downlink_end_cb() returns,
 * even after the consumer reference is released.
 *
 * @param data_available_cb Callback for when data is available.
 * @param arg Argument for the callback.
 * @return Pointer to the downlink context.
 */
struct pouch_gateway_downlink_context *pouch_gateway_downlink_open(
    pouch_gateway_downlink_data_available_cb data_available_cb,
    void *arg);

/**
 * Take the producer reference.
 *
 * Call while the consumer reference is held, before handing the context to the transport.
 * pouch_gateway_downlink_end_cb() releases it.
 *
 * @param downlink The downlink context.
 */
void pouch_gateway_downlink_acquire(struct pouch_gateway_downlink_context *downlink);

/**
 * Release the consumer reference, cancelling a running transport.
 *
 * Does not wait for running producer callbacks. Call once.
 *
 * @param downlink The downlink context.
 */
void pouch_gateway_downlink_close(struct pouch_gateway_downlink_context *downlink);

/**
 * Get data from the downlink context.
 *
 * @param downlink The downlink context.
 * @param dst Destination buffer.
 * @param[in,out] dst_len Length of the destination buffer. Set to the number of bytes written.
 * @param[out] is_last Set to true if this is the last chunk.
 *
 * @retval 0 Data was written, or none is available yet.
 * @retval -ENODATA The last chunk has already been returned.
 */
int pouch_gateway_downlink_get_data(struct pouch_gateway_downlink_context *downlink,
                                    void *dst,
                                    size_t *dst_len,
                                    bool *is_last);

/**
 * Check if the downlink is complete.
 *
 * @param downlink The downlink context.
 * @return true if complete, false otherwise.
 */
bool pouch_gateway_downlink_is_complete(const struct pouch_gateway_downlink_context *downlink);

/**
 * Block callback for downlink data.
 *
 * Called by the transport (CoAP) when a response block arrives.
 * An empty payload queues nothing and does not end the stream, even with @p is_last.
 * The first block flagged last ends the stream once it is drained.
 *
 * @param data The data received.
 * @param len The length of the data.
 * @param is_last True if this is the last block.
 * @param arg User argument (downlink context).
 * @return 0 on success, negative errno on error.
 */
int pouch_gateway_downlink_block_cb(const uint8_t *data, size_t len, bool is_last, void *arg);

/**
 * End callback for downlink.
 *
 * Called once when the transport exchange completes, after its last block callback. Ends the
 * stream once the queued blocks are drained, also when none of them was flagged last. Releases
 * the producer reference.
 *
 * @param status 0 on success, negative errno on error.
 * @param arg User argument (downlink context).
 */
void pouch_gateway_downlink_end_cb(int status, void *arg);
