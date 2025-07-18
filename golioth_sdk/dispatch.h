/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include <zephyr/sys/iterable_sections.h>

typedef void (*golioth_service_downlink_cb)(const void *data, size_t len, bool is_last);
typedef void (*golioth_service_uplink_cb)(void);

#define STREAM_ID_INVALID (-1)

struct golioth_service_data
{
    int stream_id;
};

struct golioth_service
{
    const char *path;
    golioth_service_downlink_cb downlink_cb;
    golioth_service_uplink_cb uplink_cb;
    struct golioth_service_data *data;
};

#define GOLIOTH_SERVICE(_name, _path, _downlink_cb, _uplink_cb)                           \
    BUILD_ASSERT((_path == NULL) == (_downlink_cb == NULL),                               \
                 "_path and _downlink_cb must either both be valid or both be NULL");     \
    static struct golioth_service_data _name##_data = {                                   \
        .stream_id = STREAM_ID_INVALID,                                                   \
    };                                                                                    \
    static STRUCT_SECTION_ITERABLE(golioth_service, CONCAT(_golioth_service_, _name)) = { \
        .path = _path,                                                                    \
        .downlink_cb = _downlink_cb,                                                      \
        .uplink_cb = _uplink_cb,                                                          \
        .data = &_name##_data,                                                            \
    }
