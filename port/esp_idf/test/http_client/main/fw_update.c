/*
 * Copyright (c) 2026 Golioth
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_private/startup_internal.h"
#include <esp_log.h>
#define TAG "fw_update"

#include <freertos/FreeRTOS.h>
#include <bootloader_common.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_flash_partitions.h>
#include <esp_app_format.h>
#include <esp_system.h>
#include <string.h>

#include <esp_app_desc.h>
#include <pouch/golioth/ota.h>

static esp_ota_handle_t _update_handle;
static const esp_partition_t *_update_partition = NULL;

static void ota_main_receive(const void *data, size_t offset, size_t len, bool is_last)
{
    ESP_LOGD(TAG, "Received %zu bytes at offset %zu", len, offset);

    int err = 0;

    if (0 == offset)
    {
        _update_partition = esp_ota_get_next_update_partition(NULL);
        err = esp_ota_begin(_update_partition, OTA_SIZE_UNKNOWN, &_update_handle);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
            esp_ota_abort(_update_handle);
            return;
        }

        ESP_LOGI(TAG, "Beginning package download");
    }

    err = esp_ota_write(_update_handle, (const void *) data, len);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_ota_write failed (%s)", esp_err_to_name(err));
        esp_ota_abort(_update_handle);
        return;
    }

    if (is_last)
    {
        err = esp_ota_set_boot_partition(_update_partition);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Firmware change boot image failed (%s)", esp_err_to_name(err));
            return;
        }

        int countdown = 5;
        while (countdown > 0)
        {
            ESP_LOGI(TAG, "Rebooting into new image in %d seconds", countdown);
            vTaskDelay(pdMS_TO_TICKS(1000));
            countdown--;
        }

        esp_restart();
    }
}

static void ota_manifest_receive(const struct golioth_ota_manifest_component *components,
                                 size_t num_components)
{
    for (int i = 0; i < num_components; i++)
    {
        ESP_LOGD(TAG,
                 "Target: %s@%s, %zu bytes",
                 components[i].name,
                 components[i].target,
                 components[i].size);
        ESP_LOG_BUFFER_HEXDUMP(TAG,
                               components[i].target_hash,
                               GOLIOTH_OTA_COMPONENT_HASH_BIN_LEN,
                               ESP_LOG_DEBUG);

        if (0 != strcmp(components[i].current, components[i].target))
        {
            golioth_ota_mark_for_download(components[i].name);
        }
    }
}

GOLIOTH_OTA_COMPONENT(main, "main", CONFIG_APP_PROJECT_VER, ota_main_receive);
GOLIOTH_OTA_MANIFEST_HANDLER(ota_manifest_receive);

/*
 * Add a hook used by ESP-IDF CMakeLists.txt to ensure this file is not optimized out
 */
void link_hook_fw_update_c(void) {}
