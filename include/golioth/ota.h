/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/sys/iterable_sections.h>

#define GOLIOTH_OTA_MAX_VERSION_LENGTH 32

typedef bool (*golioth_ota_component_available)(const char *name, const char *target);
typedef void (*golioth_ota_component_receive)(const void *data,
                                              size_t offset,
                                              size_t len,
                                              bool is_last);

struct golioth_ota_registered_component_data
{
    char target[GOLIOTH_OTA_MAX_VERSION_LENGTH];
    uint8_t state;
};

struct golioth_ota_registered_component
{
    const char *name;
    const char *version;
    golioth_ota_component_available available;
    golioth_ota_component_receive receive;
    struct golioth_ota_registered_component_data *data;
};

#define GOLIOTH_OTA_COMPONENT(_name, _version, _available, _receive)                               \
    static struct golioth_ota_registered_component_data ota_component_data_##_name = { \
        .target = _version,                                                            \
        .state = 0,                                                                    \
    };                                                                                 \
    static const STRUCT_SECTION_ITERABLE(golioth_ota_registered_component,             \
                                         CONCAT(ota_component_, _name)) = {            \
        .name = #_name,                                                                \
        .version = _version,                                                           \
        .available = _available,                                                       \
        .receive = _receive,                                                           \
        .data = &ota_component_data_##_name,                                           \
    }
