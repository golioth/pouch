/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <zephyr/bluetooth/conn.h>

#include <pouch/gateway/types.h>

/**
 * Get the node info for the given Bluetooth connection.
 *
 * @param conn The Bluetooth connection.
 * @return Pointer to the node info structure.
 */
struct pouch_gateway_node_info *pouch_gateway_get_node_info(const struct bt_conn *conn);

/**
 * Start Bluetooth operations for the given connection.
 *
 * @param conn The Bluetooth connection.
 */
void pouch_gateway_bt_start(struct bt_conn *conn);

/**
 * Stop Bluetooth operations for the given connection.
 *
 * @param conn The Bluetooth connection.
 */
void pouch_gateway_bt_stop(struct bt_conn *conn);

/**
 * Finish Bluetooth operations for the given connection.
 *
 * @param conn The Bluetooth connection.
 */
void pouch_gateway_bt_finished(struct bt_conn *conn);
