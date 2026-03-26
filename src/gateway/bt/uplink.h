/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

struct bt_conn;

/**
 * Start uplink for the given Bluetooth connection.
 *
 * @param conn The Bluetooth connection.
 */
void pouch_gateway_uplink_start(struct bt_conn *conn);

/**
 * Clean up uplink resources for the given Bluetooth connection.
 *
 * @param conn The Bluetooth connection.
 */
void pouch_gateway_uplink_cleanup(struct bt_conn *conn);
