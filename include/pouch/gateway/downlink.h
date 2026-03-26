/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <golioth/client.h>

struct pouch_gateway_downlink_context;
typedef void (*pouch_gateway_downlink_data_available_cb)(void *);

/**
 * Initialize the downlink module with the Golioth client.
 *
 * @param client The Golioth client.
 */
void pouch_gateway_downlink_module_init(struct golioth_client *client);

/**
 * Initialize a downlink context.
 *
 * @param data_available_cb Callback for when data is available.
 * @param arg Argument for the callback.
 * @return Pointer to the downlink context.
 */
struct pouch_gateway_downlink_context *pouch_gateway_downlink_open(
    pouch_gateway_downlink_data_available_cb data_available_cb,
    void *arg);

/**
 * Finish the downlink context.
 *
 * @param downlink The downlink context.
 */
void pouch_gateway_downlink_close(struct pouch_gateway_downlink_context *downlink);

/**
 * Abort the downlink context.
 *
 * @param downlink The downlink context.
 */
void pouch_gateway_downlink_abort(struct pouch_gateway_downlink_context *downlink);

/**
 * Get data from the downlink context.
 *
 * @param downlink The downlink context.
 * @param dst Destination buffer.
 * @param[in,out] dst_len Length of the destination buffer. Set to the number of bytes written.
 * @param[out] is_last Set to true if this is the last chunk.
 * @return 0 on success, negative on error.
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
 * @param data The data received.
 * @param len The length of the data.
 * @param is_last True if this is the last block.
 * @param arg User argument.
 * @return Golioth status.
 */
enum golioth_status pouch_gateway_downlink_block_cb(const uint8_t *data,
                                                    size_t len,
                                                    bool is_last,
                                                    void *arg);

/**
 * End callback for downlink.
 *
 * @param status Golioth status.
 * @param coap_rsp_code CoAP response code.
 * @param arg User argument.
 */
void pouch_gateway_downlink_end_cb(enum golioth_status status,
                                   const struct golioth_coap_rsp_code *coap_rsp_code,
                                   void *arg);
