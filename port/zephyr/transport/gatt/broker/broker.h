/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "types.h"

struct broker_bt_gatt_device *broker_bt_gatt_device(struct bt_conn *conn);
bool broker_bt_discovery_active(struct broker_bt_gatt_device *device, struct bt_conn *conn);
void broker_bt_disconnected(struct bt_conn *conn, uint8_t reason);
