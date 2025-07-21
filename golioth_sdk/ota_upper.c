/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ota_upper, LOG_LEVEL_DBG);

#include <zephyr/kernel.h>

#include <golioth/ota.h>

#include "ota.h"

/* Public interface to application */

/* Interface to OTA lower half */

int golioth_ota_manifest_receive_one(const struct golioth_ota_component *component)
{
    LOG_DBG("Received one component:");
    LOG_DBG("  package: %s", component->package);
    LOG_DBG("  version: %s", component->version);
    LOG_DBG("  size: %d", component->size);

    STRUCT_SECTION_FOREACH(golioth_ota_registered_component, registered)
    {
        if (0 == strcmp(component->package, registered->name))
        {
            strcpy(registered->data->target, component->version);
            if (0 != strcmp(registered->version, registered->data->target))
            {
                bool download_requested = registered->available(registered->name, registered->data->target);
                if (download_requested)
                {
                    registered->data->state = GOLIOTH_OTA_STATE_DOWNLOADING;
                }
            }
        }
    }

    return 0;
}

int golioth_ota_receive_component(const char *name,
                                  const char *version,
                                  size_t offset,
                                  const void *data,
                                  size_t len,
                                  bool is_last)
{
    static int64_t start = 0;
    if (offset == 0)
    {
        start = k_uptime_get();
    }
    else
    {
        int64_t duration = k_uptime_get() - start;
        LOG_INF("Speed %lld.%lld kB/s", offset / duration, ((offset * 10) / duration) % 10);
    }
    LOG_INF("Received %d bytes at offset %d for %s@%s", len, offset, name, version);

    STRUCT_SECTION_FOREACH(golioth_ota_registered_component, registered)
    {
        if (0 == strcmp(registered->name, name))
        {
            if (registered->data->state == GOLIOTH_OTA_STATE_DOWNLOADING)
            {
                registered->receive(data, offset, len, is_last);
            }
            else
            {
                LOG_WRN("Dropping OTA data for %s, download not requested", name);
                return -EINVAL;
            }
        }
    }

    return 0;
}

bool golioth_ota_get_status(int component_idx,
                            const char **name,
                            const char **current_version,
                            const char **target_version,
                            enum golioth_ota_state *state)
{
    int count = 0;
    STRUCT_SECTION_COUNT(golioth_ota_registered_component, &count);
    if (component_idx >= count)
    {
        return false;
    }

    struct golioth_ota_registered_component *component = NULL;
    STRUCT_SECTION_GET(golioth_ota_registered_component, component_idx, &component);

    *name = component->name;
    *current_version = component->version;
    *target_version = component->data->target;
    *state = component->data->state;

    return true;
}
