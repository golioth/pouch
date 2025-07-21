/*
 * Copyright (c) 2025 Golioth
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

#include "credentials.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/drivers/gpio.h>

#include <zephyr/dfu/flash_img.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/reboot.h>

#include <pouch/pouch.h>
#include <pouch/events.h>
#include <pouch/uplink.h>
#include <pouch/downlink.h>
#include <pouch/transport/ble_gatt/peripheral.h>
#include <pouch/transport/ble_gatt/common/types.h>

#include <golioth/golioth.h>
#include <golioth/ota.h>
#include <golioth/settings_callbacks.h>

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led0), gpios, {});

static struct
{
    uint8_t uuid[16];
    struct golioth_ble_gatt_adv_data data;
} __packed service_data = {
    .uuid = {GOLIOTH_BLE_GATT_UUID_SVC_VAL},
    .data =
        {
            .version = (POUCH_VERSION << GOLIOTH_BLE_GATT_ADV_VERSION_POUCH_SHIFT)
                | (GOLIOTH_BLE_GATT_VERSION << GOLIOTH_BLE_GATT_ADV_VERSION_SELF_SHIFT),
            .flags = 0x0,
        },
};

static struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_SVC_DATA128, &service_data, sizeof(service_data)),
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
    service_data.data.flags = 0x01;
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
        service_data.data.flags = 0x00;
        k_work_schedule(&sync_request_work, K_SECONDS(CONFIG_EXAMPLE_SYNC_PERIOD_S));
    }
}

POUCH_EVENT_HANDLER(pouch_event_handler, NULL);

static int led_setting_cb(bool new_value, void *arg)
{
    LOG_INF("LED: %d", (int) new_value);

    if (DT_HAS_ALIAS(led0))
    {
        gpio_pin_set_dt(&led, new_value ? 1 : 0);
    }

    return 0;
}

GOLIOTH_SETTINGS_HANDLER(LED, led_setting_cb, NULL);

static bool ota_main_available(const char *name, const char *target)
{
    LOG_INF("New package available %s@%s", name, target);
    return true;
}

struct flash_img_context _flash_img_context;

static int flash_img_erase_if_needed(struct flash_img_context *ctx)
{
    int err;

    if (IS_ENABLED(CONFIG_IMG_ERASE_PROGRESSIVELY))
    {
        return 0;
    }

    err = flash_area_erase(ctx->flash_area, 0, ctx->flash_area->fa_size);
    if (err)
    {
        return err;
    }

    return 0;
}

static void ota_main_receive(const void *data, size_t offset, size_t len, bool is_last)
{
    LOG_INF("Received %d bytes at offset %d", len, offset);

    int err = 0;

    if (0 == offset)
    {
        err = flash_img_init(&_flash_img_context);
        if (err)
        {
            LOG_ERR("Failed to init flash write");
            return;
        }

        err = flash_img_erase_if_needed(&_flash_img_context);
        if (err)
        {
            LOG_ERR("Failed to erase flash");
            return;
        }
    }

    err = flash_img_buffered_write(&_flash_img_context, data, len, is_last);
    if (err)
    {
        LOG_ERR("Failed to write to flash: %d", err);
        return;
    }

    if (is_last)
    {
        err = boot_request_upgrade(BOOT_UPGRADE_TEST);
        if (err)
        {
            LOG_ERR("Failed to request upgrade");
            return;
        }

        sys_reboot(SYS_REBOOT_WARM);
    }
}

GOLIOTH_OTA_COMPONENT(main, "1.5.5", ota_main_available, ota_main_receive);

int main(void)
{
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

    LOG_DBG("Bluetooth initialized");

    struct pouch_config config = {0};

#if CONFIG_POUCH_ENCRYPTION_SAEAD

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

#else  // CONFIG_POUCH_ENCRYPTION_SAEAD

    config.device_id = CONFIG_EXAMPLE_DEVICE_ID;

#endif  // CONFIG_POUCH_ENCRYPTION_SAEAD

    err = pouch_init(&config);
    if (err)
    {
        LOG_ERR("Pouch init failed (err %d)", err);
        return 0;
    }

    LOG_DBG("Pouch successfully initialized");

    err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err)
    {
        LOG_ERR("Advertising failed to start (err %d)", err);
        return 0;
    }

    LOG_DBG("Advertising successfully started");

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
