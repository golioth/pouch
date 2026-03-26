/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

struct bt_conn;
struct pouch_gateway_downlink_context;

/**
 * Start downlink for the given Bluetooth connection.
 *
 * @param conn The Bluetooth connection.
 * @return Pointer to the downlink context.
 */
struct pouch_gateway_downlink_context *pouch_gateway_downlink_start(struct bt_conn *conn);

/**
 * Clean up downlink resources for the given Bluetooth connection.
 *
 * @param conn The Bluetooth connection.
 */
void pouch_gateway_downlink_cleanup(struct bt_conn *conn);
