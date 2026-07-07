/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "nvs_flash.h"

#include <golioth/settings_callbacks.h>
#include <pouch/pouch.h>
#include <pouch/transport/serial/device.h>
#include <pouch/uplink.h>

#include "console.h"
#include "credentials.h"

static const char *TAG = "serial_device_example";

static TimerHandle_t sync_timer;
static StaticTimer_t sync_timer_buf;

static void sync_timer_cb(TimerHandle_t timer)
{
    ESP_LOGI(TAG, "Requesting sync");
    pouch_serial_device_sync();
}

static void start_sync_timer(void)
{
    sync_timer = xTimerCreateStatic("sync",
                                    pdMS_TO_TICKS(CONFIG_EXAMPLE_SYNC_PERIOD_S * 1000),
                                    pdTRUE,
                                    NULL,
                                    sync_timer_cb,
                                    &sync_timer_buf);
    if (sync_timer == NULL)
    {
        ESP_LOGE(TAG, "Failed to create sync timer");
        return;
    }

    xTimerStart(sync_timer, 0);
}

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
 * Receive settings from the cloud.
 */
static int led_setting_cb(bool new_value)
{
    ESP_LOGI(TAG, "Received LED setting: %d", (int) new_value);
    return 0;
}

GOLIOTH_SETTINGS_HANDLER(LED, led_setting_cb);

void app_main(void)
{
    ESP_LOGI(TAG, "Pouch UART Serial Device Example");

    /* Initialize NVS — required by credential storage */
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

    err = pouch_init(&config);
    if (err)
    {
        ESP_LOGE(TAG, "Pouch init failed: %d", err);
        return;
    }

    ESP_LOGI(TAG, "Pouch successfully initialized");

    /* The UART device transport is initialized at startup via POUCH_APPLICATION_STARTUP_HOOK.
     * Request an initial sync and set up a periodic sync timer.
     */
    pouch_serial_device_sync();
    start_sync_timer();

    ESP_LOGI(TAG, "UART serial device ready");
}
