/*
 * Copyright (c) 2025 Golioth
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>

#include <pouch/events.h>
#include <pouch/uplink.h>
#include <pouch/transport/toothfairy/peripheral.h>

#define SYNC_PERIOD_S 20

static uint8_t service_data[] = {TF_UUID_GOLIOTH_SVC_VAL, 0x00};

static struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_SVC_DATA128, service_data, ARRAY_SIZE(service_data)),
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err)
    {
        LOG_DBG("Connection failed (err 0x%02x)", err);
    }
    else
    {
        LOG_DBG("Connected");
    }
}

void disconnect_work_handler(struct k_work *work)
{
    int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err)
    {
        LOG_ERR("Advertising failed to start (err %d)", err);
    }
}

K_WORK_DELAYABLE_DEFINE(disconnect_work, disconnect_work_handler);

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    LOG_DBG("Disconnected (reason 0x%02x)", reason);

    k_work_schedule(&disconnect_work, K_SECONDS(1));
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

void sync_request_work_handler(struct k_work *work)
{
    service_data[ARRAY_SIZE(service_data) - 1] = 0x01;
    bt_le_adv_update_data(ad, ARRAY_SIZE(ad), NULL, 0);
}

K_WORK_DELAYABLE_DEFINE(sync_request_work, sync_request_work_handler);

static void pouch_event_handler(enum pouch_event event, void *ctx)
{
    if (POUCH_EVENT_SESSION_START == event)
    {
        pouch_uplink_entry_write(".s/sensor",
                                 POUCH_CONTENT_TYPE_JSON,
                                 "{\"temp\":22}",
                                 sizeof("{\"temp\":22}") - 1,
                                 K_FOREVER);
        pouch_uplink_close(K_FOREVER);
    }

    if (POUCH_EVENT_SESSION_END == event)
    {
        service_data[ARRAY_SIZE(service_data) - 1] = 0x00;
        k_work_schedule(&sync_request_work, K_SECONDS(SYNC_PERIOD_S));
    }
}

POUCH_EVENT_HANDLER(pouch_event_handler, NULL);

int main(void)
{
    struct toothfairy_peripheral *tf_peripheral =
        toothfairy_peripheral_create(CONFIG_POUCH_DEVICE_ID);
    if (NULL == tf_peripheral)
    {
        LOG_ERR("Failed to create toothfairy peripheral");
    }

    int err = bt_enable(NULL);
    if (err)
    {
        LOG_ERR("Bluetooth init failed (err %d)", err);
        return 0;
    }

    LOG_DBG("Bluetooth initialized\n");

    err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err)
    {
        LOG_ERR("Advertising failed to start (err %d)", err);
        return 0;
    }

    LOG_DBG("Advertising successfully started");

    k_work_schedule(&sync_request_work, K_SECONDS(SYNC_PERIOD_S));

    while (1)
    {
        k_sleep(K_SECONDS(1));
    }
    return 0;
}
