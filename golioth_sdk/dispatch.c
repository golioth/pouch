/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(glth_dispatch, CONFIG_GOLIOTH_LOG_LEVEL);

#include <pouch/downlink.h>
#include <pouch/events.h>
#include <pouch/uplink.h>

#include "dispatch.h"

static struct golioth_downlink_service *last_seen = NULL;

static void pouch_downlink_start(unsigned int stream_id, const char *path, uint16_t content_type)
{
    LOG_DBG("Downlink start: %d, %s, %d", stream_id, path, content_type);

    STRUCT_SECTION_FOREACH(golioth_downlink_service, service)
    {
        size_t path_len = strlen(service->path);
        bool partial = service->path[path_len - 1] == '*';
        if (partial)
        {
            path_len--;
        }
        if (0 == strncmp(service->path, path, path_len))
        {
            LOG_DBG("Found match for path %s", path);
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
        LOG_DBG("No handler registered for path %s", path);
    }
}

static void pouch_downlink_data(unsigned int stream_id, const void *data, size_t len, bool is_last)
{
    LOG_DBG("Downlink data: %d", stream_id);

    struct golioth_downlink_service *active_service = NULL;

    if (NULL != last_seen && last_seen->data->downlink_id == stream_id)
    {
        active_service = last_seen;
    }
    else
    {
        STRUCT_SECTION_FOREACH(golioth_downlink_service, service)
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
            active_service->data->downlink_id = DOWNLINK_ID_INVALID;
            last_seen = NULL;
        }
    }
    else
    {
        LOG_DBG("Dropping message for stream_id %d", stream_id);
    }
}

int golioth_sync_to_cloud(void)
{
    STRUCT_SECTION_FOREACH(golioth_uplink_service, service)
    {
        if (NULL != service->uplink_cb)
        {
            service->uplink_cb();
        }
    }

    pouch_uplink_close(K_FOREVER);

    return 0;
}

POUCH_DOWNLINK_HANDLER(pouch_downlink_start, pouch_downlink_data);
