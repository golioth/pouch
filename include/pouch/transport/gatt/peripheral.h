/*
 * Copyright (c) 2024 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/uuid.h>

#include <pouch/transport/gatt/common/uuids.h>

#define POUCH_GATT_ADV_DATA BT_DATA_BYTES(BT_DATA_SVC_DATA128, POUCH_GATT_UUID_SVC_VAL, 0xA5)

int pouch_gatt_peripheral_init(void);
