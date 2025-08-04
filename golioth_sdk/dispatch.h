/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include <zephyr/sys/iterable_sections.h>

typedef int golioth_downlink_id_t;

typedef void (*golioth_service_downlink_start_cb)(golioth_downlink_id_t id,
                                                  const char *path_remainder);
typedef void (*golioth_service_downlink_data_cb)(golioth_downlink_id_t id,
                                                 const void *data,
                                                 size_t len,
                                                 bool is_last);
typedef void (*golioth_service_uplink_cb)(void);

#define DOWNLINK_ID_INVALID (-1)

struct golioth_downlink_service_data
{
    golioth_downlink_id_t downlink_id;
};

struct golioth_downlink_service
{
    const char *path;
    golioth_service_downlink_start_cb start_cb;
    golioth_service_downlink_data_cb data_cb;
    struct golioth_downlink_service_data *data;
};

struct golioth_uplink_service
{
    golioth_service_uplink_cb uplink_cb;
};

#define GOLIOTH_DOWNLINK_HANDLER(_name, _path, _start_cb, _data_cb)                              \
    BUILD_ASSERT((_path != NULL) && (_data_cb != NULL), "_path, and _data_cb must not be NULL"); \
    static struct golioth_downlink_service_data _name##_data = {                                 \
        .downlink_id = DOWNLINK_ID_INVALID,                                                      \
    };                                                                                           \
    static STRUCT_SECTION_ITERABLE(golioth_downlink_service,                                     \
                                   CONCAT(_golioth_downlink_service_, _name)) = {                \
        .path = _path,                                                                           \
        .start_cb = _start_cb,                                                                   \
        .data_cb = _data_cb,                                                                     \
        .data = &_name##_data,                                                                   \
    }

#define GOLIOTH_UPLINK_HANDLER(_name, _uplink_cb)                               \
    BUILD_ASSERT(_uplink_cb != NULL, "_uplink_cb must not be NULL");            \
    static STRUCT_SECTION_ITERABLE(golioth_uplink_service,                      \
                                   CONCAT(_golioth_uplink_service_, _name)) = { \
        .uplink_cb = _uplink_cb,                                                \
    }
