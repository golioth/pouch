/*
 * Copyright (c) 2024 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/bluetooth/gatt.h>
#include <zephyr/sys/iterable_sections.h>

#define GOLIOTH_BLE_GATT_CHARACTERISTIC(name, _uuid, _props, _perm, _read, _write, _user_data) \
    STRUCT_SECTION_ITERABLE(bt_gatt_attr, name##_chrc) =                                       \
        BT_GATT_ATTRIBUTE(BT_UUID_GATT_CHRC,                                                   \
                          BT_GATT_PERM_READ,                                                   \
                          bt_gatt_attr_read_chrc,                                              \
                          NULL,                                                                \
                          ((struct bt_gatt_chrc[]) {                                           \
                              BT_GATT_CHRC_INIT(_uuid, 0U, _props),                            \
                          }));                                                                 \
    STRUCT_SECTION_ITERABLE(bt_gatt_attr, name##_val) =                                        \
        BT_GATT_ATTRIBUTE(_uuid, _perm, _read, _write, _user_data)

#define GOLIOTH_BLE_GATT_SERVICE(_uuid) \
    STRUCT_SECTION_ITERABLE(bt_gatt_attr, AAA_golioth_ble_gatt_svc) = BT_GATT_PRIMARY_SERVICE(_uuid)


STRUCT_SECTION_START_EXTERN(bt_gatt_attr);
#define GOLIOTH_BLE_GATT_ATTR_ARRAY_PTR STRUCT_SECTION_START(bt_gatt_attr)
#define GOLIOTH_BLE_GATT_ATTR_ARRAY_LEN(dst) STRUCT_SECTION_COUNT(bt_gatt_attr, dst)
