/*
 * Copyright (c) 2026 Golioth
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(fw_update_test);

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/app_version.h>
#include <psa/crypto.h>

#include <pouch/golioth/ota.h>

static void log_sha256(const char *name, const uint8_t hash[32])
{
    char hex[65];

    for (int i = 0; i < 32; i++)
    {
        snprintf(&hex[i * 2], 3, "%02x", hash[i]);
    }

    LOG_INF("OTA %s SHA256: %s", name, hex);
}

static psa_hash_operation_t hash_op;
static size_t total_received;

static void ota_main_receive(const void *data, size_t offset, size_t len, bool is_last)
{
    psa_status_t status;

    if (0 == offset)
    {
        hash_op = psa_hash_operation_init();
        status = psa_hash_setup(&hash_op, PSA_ALG_SHA_256);
        if (PSA_SUCCESS != status)
        {
            LOG_ERR("Failed to init SHA256: %d", status);
            return;
        }
        total_received = 0;
    }

    status = psa_hash_update(&hash_op, data, len);
    if (PSA_SUCCESS != status)
    {
        LOG_ERR("Failed to update SHA256: %d", status);
        return;
    }

    total_received += len;

    if (is_last)
    {
        uint8_t hash[32];
        size_t hash_len;

        status = psa_hash_finish(&hash_op, hash, sizeof(hash), &hash_len);
        if (PSA_SUCCESS != status)
        {
            LOG_ERR("Failed to finish SHA256: %d", status);
            return;
        }

        LOG_INF("OTA download complete: %zu bytes", total_received);
        log_sha256("computed", hash);

        golioth_ota_mark_idle("main");
    }
}

static void ota_manifest_receive(const struct golioth_ota_manifest_component *components,
                                 size_t num_components)
{
    for (int i = 0; i < num_components; i++)
    {
        LOG_INF("OTA manifest: %s current=%s target=%s size=%d",
                components[i].name,
                components[i].current,
                components[i].target,
                components[i].size);

        log_sha256("manifest", components[i].target_hash);

        if (0 != strcmp(components[i].current, components[i].target))
        {
            LOG_INF("Marking %s for download", components[i].name);
            golioth_ota_mark_for_download(components[i].name);
        }
    }
}

GOLIOTH_OTA_COMPONENT(main, "main", APP_VERSION_STRING, ota_main_receive);
GOLIOTH_OTA_MANIFEST_HANDLER(ota_manifest_receive);
