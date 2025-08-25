/*
 * Copyright (c) 2025 Golioth
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(fw_update);

#include <zephyr/kernel.h>
#include <zephyr/dfu/flash_img.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/reboot.h>

#include <golioth/ota.h>

#include <app_version.h>

static struct flash_img_context flash_context;

static void ota_main_receive(const void *data, size_t offset, size_t len, bool is_last)
{
    LOG_DBG("Received %d bytes at offset %d", len, offset);

    int err = 0;

    if (0 == offset)
    {
        err = flash_img_init(&flash_context);
        if (err)
        {
            LOG_ERR("Failed to init flash write");
            return;
        }
    }

    err = flash_img_buffered_write(&flash_context, data, len, is_last);
    if (err)
    {
        LOG_ERR("Failed to write to flash: %d", err);
        return;
    }

    if (is_last)
    {
        err = boot_request_upgrade(BOOT_UPGRADE_PERMANENT);
        if (err)
        {
            LOG_ERR("Failed to request upgrade");
            return;
        }

        LOG_INF("Rebooting to apply upgrade");

#if IS_ENABLED(CONFIG_LOG)
        while (log_process())
        {
        }
#endif

        k_sleep(K_SECONDS(3));

        sys_reboot(SYS_REBOOT_WARM);
    }
}

static void ota_manifest_receive(const struct golioth_ota_manifest_component *components,
                                 size_t num_components)
{
    for (int i = 0; i < num_components; i++)
    {
        LOG_DBG("Target: %s@%s, %d bytes",
                components[i].name,
                components[i].target,
                components[i].size);
        LOG_HEXDUMP_DBG(components[i].target_hash,
                        GOLIOTH_OTA_COMPONENT_HASH_BIN_LEN,
                        "Target hash:");
        if (0 != strcmp(components[i].current, components[i].target))
        {
            golioth_ota_mark_for_download(components[i].name);
        }
    }
}

GOLIOTH_OTA_COMPONENT(main, "main", APP_VERSION_STRING, ota_main_receive);
GOLIOTH_OTA_MANIFEST_HANDLER(ota_manifest_receive);
