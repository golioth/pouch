/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <esp_log.h>
#define TAG "pouch_http_client_example"

#include <pouch/pouch.h>

#include "credentials.h"

void app_main(void)
{
    ESP_LOGI(TAG, "Pouch HTTP Client Example");

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
