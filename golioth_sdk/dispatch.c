/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <pouch/downlink.h>
#include <pouch/events.h>
#include <pouch/port.h>
#include <pouch/uplink.h>
#include <string.h>

#include "dispatch.h"

POUCH_LOG_REGISTER(glth_dispatch, CONFIG_GOLIOTH_LOG_LEVEL);

static struct golioth_downlink_service *last_seen = NULL;

static void pouch_downlink_start(unsigned int stream_id, const char *path, uint16_t content_type)
{
    POUCH_LOG_DBG("Downlink start: %d, %s, %d", stream_id, path, content_type);
    POUCH_LOG_INF("Receiving Downlink entry on path %s", path);

    POUCH_STRUCT_SECTION_FOREACH(golioth_downlink_service, service)
    {
        size_t path_len = strlen(service->path);
        bool partial = service->path[path_len - 1] == '*';
        if (partial)
        {
            path_len--;
        }
        if (0 == strncmp(service->path, path, path_len))
        {
            POUCH_LOG_DBG("Found match for path %s", path);
            last_seen = service;
            service->data->downlink_id = stream_id;
            if (NULL != service->start_cb)
            {
                const char *path_remainder = NULL;
                if (partial && (strlen(path + path_len) > 0))
                {
                    path_remainder = path + path_len;
                }
                service->start_cb(stream_id, path_remainder);
            }
            break;
        }
    }

    if (NULL == last_seen)
    {
        POUCH_LOG_DBG("No handler registered for path %s", path);
    }
}

static void pouch_downlink_data(unsigned int stream_id, const void *data, size_t len, bool is_last)
{
    POUCH_LOG_DBG("Downlink data: %d", stream_id);

    struct golioth_downlink_service *active_service = NULL;

    if (NULL != last_seen && last_seen->data->downlink_id == stream_id)
    {
        active_service = last_seen;
    }
    else
    {
        POUCH_STRUCT_SECTION_FOREACH(golioth_downlink_service, service)
        {
            if (service->data->downlink_id == stream_id)
            {
                active_service = service;
                break;
            }
        }
    }

    if (NULL != active_service)
    {
        active_service->data_cb(stream_id, data, len, is_last);
        if (is_last)
        {
            POUCH_LOG_INF("Finished entry for %s", active_service->path);

            active_service->data->downlink_id = DOWNLINK_ID_INVALID;
            last_seen = NULL;
        }
    }
    else
    {
        POUCH_LOG_DBG("Dropping message for stream_id %d", stream_id);
    }
}

POUCH_DOWNLINK_HANDLER(pouch_downlink_start, pouch_downlink_data);
