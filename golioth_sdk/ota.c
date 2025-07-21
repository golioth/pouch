/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ota, LOG_LEVEL_DBG);

#include <pouch/types.h>
#include <pouch/uplink.h>

#include <zcbor_utils.h>
#include <zcbor_decode.h>

#include "dispatch.h"
#include "ota.h"

#define CONFIG_GOLIOTH_OTA_COMPONENT_NAME_LEN 16
#define CONFIG_GOLIOTH_OTA_COMPNENT_VERSION_LEN 16

struct component_tstr_value
{
    char *value;
    size_t value_len;
};

enum
{
    MANIFEST_KEY_SEQUENCE_NUMBER = 1,
    MANIFEST_KEY_HASH = 2,
    MANIFEST_KEY_COMPONENTS = 3,
};

enum
{
    COMPONENT_KEY_PACKAGE = 1,
    COMPONENT_KEY_VERSION = 2,
    COMPONENT_KEY_HASH = 3,
    COMPONENT_KEY_SIZE = 4,
    COMPONENT_KEY_URI = 5,
    COMPONENT_KEY_BOOTLOADER = 6,
};

struct
{
    golioth_downlink_id_t downlink_id;
    size_t offset;
    char name[CONFIG_GOLIOTH_OTA_COMPONENT_NAME_LEN];
    char version[CONFIG_GOLIOTH_OTA_COMPNENT_VERSION_LEN];
} component_download = {
    .downlink_id = DOWNLINK_ID_INVALID,
};

static int component_entry_decode_value(zcbor_state_t *zsd, void *void_value)
{
    struct component_tstr_value *value = void_value;
    struct zcbor_string tstr;
    bool ok;

    ok = zcbor_tstr_decode(zsd, &tstr);
    if (!ok)
    {
        return -EBADMSG;
    }

    if (tstr.len > value->value_len)
    {
        LOG_ERR("Not enough space to store");
        return -ENOMEM;
    }

    memcpy(value->value, tstr.value, tstr.len);
    value->value[tstr.len] = '\0';

    return 0;
}

static void ota_receive_manifest(golioth_downlink_id_t id,
                                 const void *data,
                                 size_t len,
                                 bool is_last)
{
    ZCBOR_STATE_D(zsd, 3, data, len, 1, 0);

    bool ok = zcbor_map_start_decode(zsd);
    if (!ok)
    {
        LOG_ERR("Failed to deserialize manifest");
        return;
    }

    while (!zcbor_list_or_map_end(zsd))
    {
        uint32_t key;
        ok = zcbor_uint32_decode(zsd, &key);
        if (!ok)
        {
            LOG_ERR("Failed to deserialize manifest");
            return;
        }

        if (MANIFEST_KEY_COMPONENTS == key)
        {
            ok = zcbor_list_start_decode(zsd);
            if (!ok)
            {
                LOG_ERR("Failed to deserialize manifest");
                return;
            }

            while (!zcbor_list_or_map_end(zsd))
            {
                struct golioth_ota_component component = {};
                struct component_tstr_value package = {
                    component.package,
                    sizeof(component.package) - 1,
                };
                struct component_tstr_value version = {
                    component.version,
                    sizeof(component.version) - 1,
                };
                char hash_string[GOLIOTH_OTA_COMPONENT_HEX_HASH_LEN + 1];
                struct component_tstr_value hash = {
                    hash_string,
                    sizeof(hash_string) - 1,
                };

                struct zcbor_map_entry map_entries[] = {
                    ZCBOR_U32_MAP_ENTRY(COMPONENT_KEY_PACKAGE,
                                        component_entry_decode_value,
                                        &package),
                    ZCBOR_U32_MAP_ENTRY(COMPONENT_KEY_VERSION,
                                        component_entry_decode_value,
                                        &version),
                    ZCBOR_U32_MAP_ENTRY(COMPONENT_KEY_HASH, component_entry_decode_value, &hash),
                    ZCBOR_U32_MAP_ENTRY(COMPONENT_KEY_SIZE,
                                        zcbor_map_int32_decode,
                                        &component.size),
                };

                int err = zcbor_map_decode(zsd, map_entries, ARRAY_SIZE(map_entries));
                if (err)
                {
                    LOG_ERR("Failed to deserialize manifest");
                    return;
                }

                golioth_ota_manifest_receive_one(&component);
            }

            ok = zcbor_list_or_map_end(zsd);
            if (!ok)
            {
                LOG_ERR("Failed to deserialize manifest");
                return;
            }
        }
        else
        {
            ok = zcbor_any_skip(zsd, NULL);
            if (!ok)
            {
                LOG_ERR("Failed to deserialize manifest");
                return;
            }
        }
    }
}

static void ota_receive_component_start(golioth_downlink_id_t id, const char *path_remainder)
{
    LOG_INF("Received component start on path %s, id %d", path_remainder, id);
    const char *delimiter = strchr(path_remainder, '@');
    if (NULL != delimiter)
    {
        component_download.downlink_id = id;
        size_t name_len = ((intptr_t) delimiter) - ((intptr_t) path_remainder);
        strncpy(component_download.name, path_remainder, name_len);
        component_download.name[name_len] = '\0';
        strcpy(component_download.version, delimiter + 1);
        component_download.offset = 0;
    }
    else
    {
        component_download.downlink_id = DOWNLINK_ID_INVALID;
    }
}

static void ota_receive_component_data(golioth_downlink_id_t id,
                                       const void *data,
                                       size_t len,
                                       bool is_last)
{
    LOG_INF("Received %d bytes on downlink id %d", len, id);
    if (DOWNLINK_ID_INVALID != component_download.downlink_id)
    {
        golioth_ota_receive_component(component_download.name,
                                      component_download.version,
                                      component_download.offset,
                                      data,
                                      len,
                                      is_last);
        component_download.offset += len;
    }
}

static void ota_uplink(void)
{
    const char *name = NULL;
    const char *current_version = NULL;
    const char *target_version = NULL;
    enum golioth_ota_state state = GOLIOTH_OTA_STATE_IDLE;
    int component_idx = 0;

    while (golioth_ota_get_status(component_idx++, &name, &current_version, &target_version, &state))
    {
        uint8_t encode_buf[64];
        ZCBOR_STATE_E(zse, 1, encode_buf, sizeof(encode_buf), 1);

        bool ok = zcbor_map_start_encode(zse, 1);
        if (!ok)
        {
            return;
        }

        ok = zcbor_tstr_put_lit(zse, "s") && zcbor_uint32_put(zse, state);
        if (!ok)
        {
            return;
        }

        ok = zcbor_tstr_put_lit(zse, "r") && zcbor_uint32_put(zse, 0);
        if (!ok)
        {
            return;
        }

        ok = zcbor_tstr_put_lit(zse, "pkg") && zcbor_tstr_put_term(zse, name, SIZE_MAX);
        if (!ok)
        {
            return;
        }

        ok = zcbor_tstr_put_lit(zse, "v") && zcbor_tstr_put_term(zse, current_version, SIZE_MAX);
        if (!ok)
        {
            return;
        }

        ok = zcbor_tstr_put_lit(zse, "t") && zcbor_tstr_put_term(zse, target_version, SIZE_MAX);
        if (!ok)
        {
            return;
        }

        ok = zcbor_map_end_encode(zse, 1);
        if (!ok)
        {
            return;
        }

        char path[64] = ".u/c/";
        strncpy(&path[strlen(".u/c/")], name, sizeof(path) - strlen(".u/c/"));
        path[sizeof(path) - 1] = '\0';
        pouch_uplink_entry_write(path,
                                 POUCH_CONTENT_TYPE_CBOR,
                                 encode_buf,
                                 zse->payload - encode_buf,
                                 K_FOREVER);
    }
}

GOLIOTH_DOWNLINK_HANDLER(ota_manifest, "/.u/desired", NULL, ota_receive_manifest);
GOLIOTH_DOWNLINK_HANDLER(ota_component,
                         "/.u/c/*",
                         ota_receive_component_start,
                         ota_receive_component_data);
GOLIOTH_UPLINK_HANDLER(ota_status, ota_uplink);
