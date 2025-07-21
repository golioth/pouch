/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ota_upper, LOG_LEVEL_DBG);

#include <golioth/ota.h>

#include "ota.h"

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
        }
    }

    return 0;
}

bool golioth_ota_get_status(int component_idx, const char **name, const char **current_version, const char **target_version)
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

    return true;
}

