/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/sys/iterable_sections.h>

#define GOLIOTH_OTA_COMPONENT_HASH_BIN_LEN 32

/* Manifests handler registration */

struct golioth_ota_manifest_component
{
    const char *name;
    const char *current;
    const char *target;
    const uint8_t *target_hash;
    size_t size;
};

typedef void (*golioth_ota_manifest_receive)(
    const struct golioth_ota_manifest_component *components,
    size_t num_components);

struct golioth_ota_manifest_handler
{
    golioth_ota_manifest_receive receive;
};

/* Note: Only one handler can be registered */
#define GOLIOTH_OTA_MANIFEST_HANDLER(_handler)                                            \
    const STRUCT_SECTION_ITERABLE(golioth_ota_manifest_handler, ota_manifest_handler) = { \
        .receive = _handler,                                                              \
    }

/* Component handler registration */

typedef void (*golioth_ota_component_receive)(const void *data,
                                              size_t offset,
                                              size_t len,
                                              bool is_last);

struct golioth_ota_registered_component_data
{
    char target[CONFIG_GOLIOTH_OTA_MAX_VERSION_LEN];
    uint8_t target_hash[GOLIOTH_OTA_COMPONENT_HASH_BIN_LEN];
    size_t size;
    uint8_t state;
};

struct golioth_ota_registered_component
{
    const char *name;
    const char *version;
    golioth_ota_component_receive receive;
    struct golioth_ota_registered_component_data *data;
};

#define GOLIOTH_OTA_COMPONENT(_name, _package, _version, _receive)              \
    struct golioth_ota_registered_component_data ota_component_data_##_name = { \
        .target = _version,                                                     \
        .state = 0,                                                             \
    };                                                                          \
    const STRUCT_SECTION_ITERABLE(golioth_ota_registered_component,             \
                                  CONCAT(ota_component_, _name)) = {            \
        .name = _package,                                                       \
        .version = _version,                                                    \
        .receive = _receive,                                                    \
        .data = &ota_component_data_##_name,                                    \
    }

/* Component state API */

int golioth_ota_mark_for_download(const char *name);
int golioth_ota_mark_idle(const char *name);
int golioth_ota_mark_updating(const char *name);
