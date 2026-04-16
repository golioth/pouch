/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <esp_log.h>
#define TAG "pouch_http_client_example"

#include <pouch/pouch.h>

#include "credentials.h"
#include "nvs_flash.h"
#include "wifi.h"

void app_main(void)
{
    ESP_LOGI(TAG, "Pouch HTTP Client Example");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    if (ESP_OK != ret)
    {
        ESP_LOGE(TAG, "Failed to initialize NVS: %d", ret);
        return;
    }

    ESP_LOGI(TAG, "Initializing WiFi");
    wifi_init_sta();

    struct pouch_config config = {0};
    int err = fill_pouch_config(&config);
    if (0 != err)
    {
        ESP_LOGE(TAG, "Failed to fill config");
        return;
    }

    err = pouch_init(&config);
    if (0 != err)
    {
        ESP_LOGE(TAG, "Failed to init pouch: %d", err);
        return;
    }
    ESP_LOGI(TAG, "Pouch successfully initialized");
}
