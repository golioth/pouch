/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct pouch_block;
struct pouch_gateway_downlink_context;
struct pouch_gateway_uplink;

enum pouch_gateway_uplink_result
{
    POUCH_GATEWAY_UPLINK_SUCCESS,
    POUCH_GATEWAY_UPLINK_ERROR_LOCAL,
    POUCH_GATEWAY_UPLINK_ERROR_CLOUD,
};

typedef void (*pouch_gateway_uplink_end_cb)(void *arg, enum pouch_gateway_uplink_result res);

/**
 * Write data to the uplink.
 *
 * @param uplink The uplink context.
 * @param payload The payload to write.
 * @param len The length of the payload.
 * @param is_last true if this is the last chunk.
 * @return 0 on success, negative on error.
 */
int pouch_gateway_uplink_write(struct pouch_gateway_uplink *uplink,
                               const uint8_t *payload,
                               size_t len,
                               bool is_last);

/**
 * Open an uplink for the given downlink context.
 *
 * The uplink must be closed by a call to @ref pouch_gateway_uplink_close().
 *
 * @param downlink The downlink context.
 * @param end_cb Callback invoked when the uplink exchange completes.
 * @param end_cb_arg Argument for @p end_cb.
 * @return Pointer to the uplink context.
 */
struct pouch_gateway_uplink *pouch_gateway_uplink_open(
    struct pouch_gateway_downlink_context *downlink,
    pouch_gateway_uplink_end_cb end_cb,
    void *end_cb_arg);

/**
 * Close the uplink.
 *
 * Triggers the CoAP POST to forward all buffered data to the server.
 *
 * @param uplink The uplink context.
 */
void pouch_gateway_uplink_close(struct pouch_gateway_uplink *uplink);

/**
 * Initialize the uplink module.
 */
void pouch_gateway_uplink_module_init(void);
