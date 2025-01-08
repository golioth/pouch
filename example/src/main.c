/*
 * Copyright (c) 2025 Golioth
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>

#include <toothfairy/peripheral.h>

static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        LOG_DBG("Connection failed (err 0x%02x)", err);
    } else {
        LOG_DBG("Connected");
    }
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    LOG_DBG("Disconnected (reason 0x%02x)", reason);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

static struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    TOOTHFAIRY_ADV_DATA,
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

int main(void)
{
    struct toothfairy_peripheral *tf_peripheral = toothfairy_peripheral_create();
    if (NULL == tf_peripheral)
    {
        LOG_ERR("Failed to create toothfairy peripheral");
    }

    int err = bt_enable(NULL);
    if (err) {
        LOG_ERR("Bluetooth init failed (err %d)", err);
        return 0;
    }

    LOG_DBG("Bluetooth initialized\n");

    err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err) {
        LOG_ERR("Advertising failed to start (err %d)", err);
        return 0;
    }

    LOG_DBG("Advertising successfully started");

    while (1) {
        k_sleep(K_SECONDS(1));
    }
    return 0;
}
