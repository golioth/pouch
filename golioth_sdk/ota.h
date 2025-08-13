/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define GOLIOTH_OTA_COMPONENT_HASH_BIN_LEN 32

enum golioth_ota_state
{
    GOLIOTH_OTA_STATE_IDLE,
    GOLIOTH_OTA_STATE_DOWNLOADING,
    GOLIOTH_OTA_STATE_DOWNLOADED,
    GOLIOTH_OTA_STATE_UPDATING,
};

struct golioth_ota_component
{
    char package[CONFIG_GOLIOTH_OTA_MAX_PACKAGE_NAME_LEN + 1];
    char version[CONFIG_GOLIOTH_OTA_MAX_VERSION_LEN + 1];
    uint8_t hash[GOLIOTH_OTA_COMPONENT_HASH_BIN_LEN];
    int32_t size;
};

/* To be implemented by upper half */

int golioth_ota_manifest_receive_one(const struct golioth_ota_component *component);
void golioth_ota_manifest_complete(void);
int golioth_ota_receive_component(const char *name,
                                  const char *version,
                                  size_t offset,
                                  const void *data,
                                  size_t len,
                                  bool is_last);
bool golioth_ota_get_status(int component_idx,
                            const char **name,
                            const char **current_version,
                            const char **target_version,
                            enum golioth_ota_state *state);
