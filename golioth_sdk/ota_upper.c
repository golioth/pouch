/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ota_upper, CONFIG_GOLIOTH_LOG_LEVEL);

#include <zephyr/kernel.h>

#include <golioth/ota.h>

#include "ota.h"

/* Public interface to application */

static int golioth_ota_set_status(const char *name, enum golioth_ota_state state)
{
    int ret = -ENOENT;

    STRUCT_SECTION_FOREACH(golioth_ota_registered_component, component)
    {
        if (0 == strcmp(component->name, name))
        {
            component->data->state = state;
            ret = 0;
            break;
        }
    }

    return ret;
}

int golioth_ota_mark_for_download(const char *name)
{
    return golioth_ota_set_status(name, GOLIOTH_OTA_STATE_DOWNLOADING);
}

int golioth_ota_mark_idle(const char *name)
{
    return golioth_ota_set_status(name, GOLIOTH_OTA_STATE_IDLE);
}

int golioth_ota_mark_updating(const char *name)
{
    return golioth_ota_set_status(name, GOLIOTH_OTA_STATE_UPDATING);
}

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
            strncpy(registered->data->target, component->version, sizeof(registered->data->target));
            registered->data->target[sizeof(registered->data->target) - 1] = '\0';
            memcpy(registered->data->target_hash,
                   component->hash,
                   GOLIOTH_OTA_COMPONENT_HASH_BIN_LEN);
            registered->data->size = component->size;
        }
    }

    return 0;
}

void golioth_ota_manifest_complete(void)
{
    size_t num_components = 0;
    STRUCT_SECTION_COUNT(golioth_ota_registered_component, &num_components);
    struct golioth_ota_manifest_component components[num_components];

    for (int i = 0; i < num_components; i++)
    {
        struct golioth_ota_registered_component *registered;
        STRUCT_SECTION_GET(golioth_ota_registered_component, i, &registered);
        components[i].name = registered->name;
        components[i].current = registered->version;
        components[i].target = registered->data->target;
        components[i].target_hash = registered->data->target_hash;
        components[i].size = registered->data->size;
    }

    STRUCT_SECTION_FOREACH(golioth_ota_manifest_handler, handler)
    {
        handler->receive(components, num_components);
    }
}

int golioth_ota_receive_component(const char *name,
                                  const char *version,
                                  size_t offset,
                                  const void *data,
                                  size_t len,
                                  bool is_last)
{
    LOG_DBG("Received %d bytes at offset %d for %s@%s", len, offset, name, version);

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
