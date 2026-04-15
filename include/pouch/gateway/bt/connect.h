/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <zephyr/bluetooth/conn.h>

/** Bluetooth gateway end callback */
typedef void (*pouch_gateway_bt_end_t)(struct bt_conn *conn);

/**
 * Start Bluetooth Pouch gateway for the given connection.
 *
 * @param conn The Bluetooth connection.
 * @param callback Callback to call when the gateway operation finished for the given connection.
 */
int pouch_gateway_bt_start(struct bt_conn *conn, pouch_gateway_bt_end_t callback);
