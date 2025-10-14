/*
 * Copyright (c) 2025 Golioth
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main);

#include "credentials.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/drivers/gpio.h>

#include <pouch/pouch.h>
#include <pouch/events.h>
#include <pouch/uplink.h>
#include <pouch/downlink.h>
#include <pouch/transport/ble_gatt/peripheral.h>
#include <pouch/transport/ble_gatt/common/types.h>

#include <golioth/golioth.h>
#include <golioth/settings_callbacks.h>

#include <app_version.h>

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led0), gpios, {});

static struct
{
    uint16_t uuid;
    struct golioth_ble_gatt_adv_data data;
} __packed service_data = {
    .uuid = GOLIOTH_BLE_GATT_UUID_SVC_VAL_16,
    .data =
        {
            .version = (POUCH_VERSION << GOLIOTH_BLE_GATT_ADV_VERSION_POUCH_SHIFT)
                | (GOLIOTH_BLE_GATT_VERSION << GOLIOTH_BLE_GATT_ADV_VERSION_SELF_SHIFT),
            .flags = 0x0,
        },
};

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
    service_data.data.flags |= GOLIOTH_BLE_GATT_ADV_FLAG_SYNC_REQUEST;
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

        golioth_sync_to_cloud();
    }

    if (POUCH_EVENT_SESSION_END == event)
    {
        service_data.data.flags &= ~GOLIOTH_BLE_GATT_ADV_FLAG_SYNC_REQUEST;
        bt_le_adv_update_data(ad, ARRAY_SIZE(ad), NULL, 0);
        k_work_schedule(&sync_request_work, K_SECONDS(CONFIG_EXAMPLE_SYNC_PERIOD_S));
    }
}

POUCH_EVENT_HANDLER(pouch_event_handler, NULL);

static int led_setting_cb(bool new_value)
{
    LOG_INF("Received LED setting: %d", (int) new_value);

    if (DT_HAS_ALIAS(led0))
    {
        gpio_pin_set_dt(&led, new_value ? 1 : 0);
    }

    return 0;
}

GOLIOTH_SETTINGS_HANDLER(LED, led_setting_cb);

int main(void)
{
    LOG_INF("Pouch SDK Version: " STRINGIFY(APP_BUILD_VERSION));
    LOG_INF("Pouch Protocol Version: %d", POUCH_VERSION);
    LOG_INF("Pouch BLE Transport Protocol Version: %d", GOLIOTH_BLE_GATT_VERSION);

    int err = golioth_ble_gatt_peripheral_init();
    if (err)
    {
        LOG_ERR("Failed to initialize Pouch BLE GATT peripheral (err %d)", err);
        return 0;
    }

    err = bt_enable(NULL);
    if (err)
    {
        LOG_ERR("Bluetooth init failed (err %d)", err);
        return 0;
    }

    LOG_INF("Bluetooth initialized");

    struct pouch_config config = {0};

    err = load_certificate(&config.certificate);
    if (err)
    {
        LOG_ERR("Failed to load certificate (err %d)", err);
        return 0;
    }

    config.private_key = load_private_key();
    if (config.private_key == PSA_KEY_ID_NULL)
    {
        LOG_ERR("Failed to load private key");
        return 0;
    }

    LOG_INF("Credentials loaded");

    err = pouch_init(&config);
    if (err)
    {
        LOG_ERR("Pouch init failed (err %d)", err);
        return 0;
    }

    LOG_INF("Pouch initialized");

    err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err)
    {
        LOG_ERR("Advertising failed to start (err %d)", err);
        return 0;
    }

    LOG_INF("Advertising started");

    if (DT_HAS_ALIAS(led0))
    {
        err = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
        if (err < 0)
        {
            LOG_ERR("Could not initialize LED");
        }
    }

    k_work_schedule(&sync_request_work, K_SECONDS(CONFIG_EXAMPLE_SYNC_PERIOD_S));

    while (1)
    {
        k_sleep(K_SECONDS(1));
    }
    return 0;
}
