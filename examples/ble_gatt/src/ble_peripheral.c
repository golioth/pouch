/*
 * Copyright (c) 2026 Golioth
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(example_ble_peripheral);

#include "ble_peripheral.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>

#include <pouch/transport/bluetooth/gatt.h>

static struct bt_conn *default_conn;

static struct pouch_gatt_adv service_data = POUCH_GATT_ADV_DATA_INIT;

static struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_SVC_DATA16, &service_data, sizeof(service_data)),
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err)
    {
        LOG_DBG("Connection failed (err 0x%02x)", err);
        return;
    }

    LOG_DBG("Connected");
    default_conn = conn;
}

static void sync_request_work_handler(struct k_work *work)
{
    LOG_DBG("Requesting Gateway sync");
    ble_peripheral_request_gateway(true);
}
K_WORK_DELAYABLE_DEFINE(sync_request_work, sync_request_work_handler);

static void resume_advertising(struct k_work *work)
{
    int err = ble_peripheral_start();
    if (err)
    {
        LOG_ERR("Failed to start advertising (err: %d)", err);
        return;
    }

    // Although advertising resumes right away, we don't have to request a gateway.
    // Schedule the gateway flag to be set at a later time:
    k_work_schedule(&sync_request_work, K_SECONDS(CONFIG_EXAMPLE_SYNC_PERIOD_S));
}
K_WORK_DEFINE(resume_work, resume_advertising);

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    LOG_DBG("Disconnected (reason 0x%02x)", reason);

    default_conn = NULL;
    k_work_submit(&resume_work);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
    char addr[BT_ADDR_LE_STR_LEN];
    char passkey_str[7];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    (void) snprintf(passkey_str, sizeof(passkey_str), "%06u", passkey);

    LOG_INF("Passkey for %s: %s", addr, passkey_str);
}

static void auth_passkey_confirm(struct bt_conn *conn, unsigned int passkey)
{
    char addr[BT_ADDR_LE_STR_LEN];
    char passkey_str[7];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    (void) snprintf(passkey_str, sizeof(passkey_str), "%06u", passkey);

    LOG_INF("Confirm passkey for %s: %s", addr, passkey_str);

    if (IS_ENABLED(CONFIG_EXAMPLE_BT_AUTO_CONFIRM))
    {
        LOG_INF("Confirming passkey");
        bt_conn_auth_passkey_confirm(conn);
    }
}

static void auth_cancel(struct bt_conn *conn)
{
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    LOG_INF("Pairing cancelled: %s", addr);
}

static struct bt_conn_auth_cb auth_cb_display = {
    .passkey_display = auth_passkey_display,
    .passkey_confirm = auth_passkey_confirm,
    .cancel = auth_cancel,
};

void ble_peripheral_request_gateway(bool request)
{
    pouch_gatt_adv_req_sync(&service_data, request);
    bt_le_adv_update_data(ad, ARRAY_SIZE(ad), NULL, 0);
}

void ble_peripheral_button_handler(void)
{
    if (default_conn)
    {
        LOG_INF("Confirming passkey");
        bt_conn_auth_passkey_confirm(default_conn);
    }
    else
    {
        LOG_WRN("No BT connection for passkey confirmation");
    }
}

int ble_peripheral_init(void)
{
    int err = bt_enable(NULL);
    if (err)
    {
        LOG_ERR("Bluetooth init failed (err %d)", err);
        return err;
    }

    err = bt_conn_auth_cb_register(&auth_cb_display);
    if (err)
    {
        LOG_ERR("Bluetooth auth cb register failed (err %d)", err);
        return err;
    }

    return 0;
}

int ble_peripheral_start(void)
{
    pouch_gatt_adv_req_sync(&service_data, false);
    return bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, ad, ARRAY_SIZE(ad), NULL, 0);
}
