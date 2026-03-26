/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

struct bt_conn;

/**
 * Start reading info characteristic for the given Bluetooth connection.
 *
 * @param conn The Bluetooth connection.
 */
void pouch_gateway_info_read_start(struct bt_conn *conn);
