/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

struct bt_conn;

/**
 * Start server certificate download for the given Bluetooth connection.
 *
 * @param conn The Bluetooth connection.
 */
void pouch_gateway_server_cert_write(struct bt_conn *conn);

/**
 * Start device certificate upload for the given Bluetooth connection.
 *
 * @param conn The Bluetooth connection.
 */
void pouch_gateway_device_cert_read(struct bt_conn *conn);
