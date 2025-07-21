/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define GOLIOTH_OTA_COMPONENT_BIN_HASH_LEN 32
#define GOLIOTH_OTA_COMPONENT_HEX_HASH_LEN 64
#define CONFIG_GOLIOTH_OTA_MAX_PACKAGE_NAME_LEN 32
#define CONFIG_GOLIOTH_OTA_MAX_VERSION_LEN 32

struct golioth_ota_component
{
    char package[CONFIG_GOLIOTH_OTA_MAX_PACKAGE_NAME_LEN + 1];
    char version[CONFIG_GOLIOTH_OTA_MAX_VERSION_LEN + 1];
    int32_t size;
    uint8_t hash[GOLIOTH_OTA_COMPONENT_BIN_HASH_LEN];
};

int golioth_ota_manifest_receive_one(const struct golioth_ota_component *component);
bool golioth_ota_get_status(int component_idx, const char **name, const char **current_version, const char **target_version);
