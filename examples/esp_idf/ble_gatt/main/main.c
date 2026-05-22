/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include "esp_log.h"
#include "nvs_flash.h"

#include <pouch/pouch.h>
#include <pouch/uplink.h>
#include <pouch/downlink.h>

#include "ble_peripheral.h"
#include "console.h"
#include "credentials.h"

static const char *TAG = "ble_gatt_example";

/**
 * Push application data to the cloud on every uplink.
 */
static void do_uplink(void)
{
    const char *data = "{\"temp\":22}";
    pouch_uplink_entry_write(".s/sensor",
                             POUCH_CONTENT_TYPE_JSON,
                             data,
                             strlen(data),
                             POUCH_FOREVER);
}

POUCH_UPLINK_HANDLER(do_uplink);

/**
 * Receive downlink data from the cloud (hexdump only, no processing).
 */
static void downlink_start(unsigned int stream_id, const char *path, uint16_t content_type)
{
    ESP_LOGI(TAG,
             "Downlink start: path=%s content_type=%u stream_id=%u",
             path,
             content_type,
             stream_id);
}

static void downlink_data(unsigned int stream_id, const void *data, size_t len, bool is_last)
{
    ESP_LOGI(TAG,
             "Downlink data: len=%u stream_id=%u is_last=%d",
             (unsigned) len,
             stream_id,
             (int) is_last);
    ESP_LOG_BUFFER_HEXDUMP(TAG, data, len, ESP_LOG_INFO);
}

POUCH_DOWNLINK_HANDLER(downlink_start, downlink_data);

void app_main(void)
{
    ESP_LOGI(TAG, "Pouch BLE GATT Example");

    /* Initialize NVS — required by BT stack and credential storage */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Start console for credential provisioning */
    console_init();

    /* Load credentials from NVS */
    struct pouch_config config = {0};

    int err = credentials_init(&config);
    if (err)
    {
        ESP_LOGW(TAG, "Credentials not provisioned. Use console to set crt/key, then reset.");
        return;
    }

    /* Initialize BLE and register pouch GATT service */
    err = ble_peripheral_init();
    if (err)
    {
        ESP_LOGE(TAG, "BLE init failed");
        return;
    }

    err = pouch_init(&config);
    if (err)
    {
        ESP_LOGE(TAG, "Pouch init failed: %d", err);
        return;
    }

    ESP_LOGI(TAG, "Pouch initialized");

    /* Start advertising */
    err = ble_peripheral_start();
    if (err)
    {
        ESP_LOGE(TAG, "BLE advertising start failed");
        return;
    }

    /* Request a gateway right away */
    ble_peripheral_request_gateway(true);

    ESP_LOGI(TAG, "BLE GATT peripheral ready");
}
