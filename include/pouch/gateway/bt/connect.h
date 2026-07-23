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
 * A zero return accepts the operation. The callback is invoked exactly once
 * after broker cleanup if the operation terminates while the connection is
 * still active. Physical disconnection is reported through the Zephyr
 * @ref bt_conn_cb.disconnected callback and does not invoke this callback.
 *
 * A negative return rejects the operation and does not invoke the callback.
 * Only one operation may own a Bluetooth connection slot at a time.
 *
 * @param conn The Bluetooth connection.
 * @param callback Callback to call when the gateway operation finished for the given connection.
 *
 * @retval 0 Operation accepted.
 * @retval -EINVAL Invalid connection or callback.
 * @retval -ENOTCONN The connection could not be retained.
 * @retval -EBUSY The connection slot is already in use.
 * @return Other negative errors when discovery could not be queued.
 */
int pouch_gateway_bt_start(struct bt_conn *conn, pouch_gateway_bt_end_t callback);
