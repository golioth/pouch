/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(settings);

#include <zephyr/drivers/gpio.h>

#include <pouch/types.h>
#include <pouch/uplink.h>
#include <zcbor_utils.h>

#include <zcbor_decode.h>

#include <golioth/settings_types.h>

#include "dispatch.h"
#include "settings.h"

#define SETTINGS_DOWNLINK_PATH "/.c"
#define SETTINGS_UPLINK_PATH ".c/status"

#define GOLIOTH_SETTINGS_MAX_NAME_LEN 31

static int64_t settings_version = -1;

static int settings_decode(zcbor_state_t *zsd, void *value)
{
    struct zcbor_string label;
    bool ok;

    if (zcbor_nil_expect(zsd, NULL))
    {
        /* No settings are set */
        return -ENOENT;
    }

    ok = zcbor_map_start_decode(zsd);
    if (!ok)
    {
        LOG_WRN("Did not start CBOR list correctly");
        return -EBADMSG;
    }

    while (!zcbor_list_or_map_end(zsd))
    {
        struct setting_value value;
        memset(&value, 0, sizeof(value));

        bool data_type_valid = true;

        /* Handle item */
        ok = zcbor_tstr_decode(zsd, &label);
        if (!ok)
        {
            LOG_ERR("Failed to get label");
            return -EBADMSG;
        }

        char key[GOLIOTH_SETTINGS_MAX_NAME_LEN + 1] = {};

        /* Copy setting label/name and ensure it's NULL-terminated */
        memcpy(key, label.value, MIN(GOLIOTH_SETTINGS_MAX_NAME_LEN, label.len));

        value.key = key;

        zcbor_major_type_t major_type = ZCBOR_MAJOR_TYPE(*zsd->payload);

        switch (major_type)
        {
            case ZCBOR_MAJOR_TYPE_TSTR:
            {
                struct zcbor_string str;

                ok = zcbor_tstr_decode(zsd, &str);
                if (!ok)
                {
                    data_type_valid = false;
                    break;
                }

                value.type = GOLIOTH_SETTING_VALUE_TYPE_STRING;
                value.str_val.data = str.value;
                value.str_val.len = str.len;
                break;
            }
            case ZCBOR_MAJOR_TYPE_PINT:
            case ZCBOR_MAJOR_TYPE_NINT:
            {
                value.type = GOLIOTH_SETTING_VALUE_TYPE_INT;
                ok = zcbor_int32_decode(zsd, &value.int_val);
                if (!ok)
                {
                    data_type_valid = false;
                    break;
                }

                break;
            }
            case ZCBOR_MAJOR_TYPE_SIMPLE:
            {
                if (zcbor_float_decode(zsd, &value.float_val))
                {
                    value.type = GOLIOTH_SETTING_VALUE_TYPE_FLOAT;
                }
                else if (zcbor_bool_decode(zsd, &value.bool_val))
                {
                    value.type = GOLIOTH_SETTING_VALUE_TYPE_BOOL;
                }
                else
                {
                    data_type_valid = false;
                }
                break;
            }
            default:
                data_type_valid = false;
                break;
        }

        if (data_type_valid)
        {
            golioth_settings_receive_one(&value);
        }
        else
        {
            ok = zcbor_any_skip(zsd, NULL);
            if (!ok)
            {
                LOG_ERR("Failed to skip unsupported type");
                return -EBADMSG;
            }
        }
    }

    ok = zcbor_map_end_decode(zsd);
    if (!ok)
    {
        LOG_WRN("Did not end CBOR list correctly");
        return -EBADMSG;
    }

    return 0;
}

static void settings_downlink(const void *data, size_t len, bool is_last)
{
    LOG_DBG("Received settings downlink");

    ZCBOR_STATE_D(zsd, 2, data, len, 1, 0);

    struct zcbor_map_entry map_entries[] = {
        ZCBOR_TSTR_LIT_MAP_ENTRY("settings", settings_decode, NULL),
        ZCBOR_TSTR_LIT_MAP_ENTRY("version", zcbor_map_int64_decode, &settings_version),
    };

    zcbor_map_decode(zsd, map_entries, ARRAY_SIZE(map_entries));
}

static void settings_uplink(void)
{
    zcbor_state_t zse[3];
    uint8_t buf[64];
    zcbor_new_encode_state(zse, 3, buf, sizeof(buf), 1);

    zcbor_map_start_encode(zse, 2);
    zcbor_tstr_put_lit(zse, "version");
    if (settings_version >= 0)
    {
        zcbor_int64_put(zse, settings_version);
    }
    else
    {
        zcbor_nil_put(zse, NULL);
    }
    zcbor_map_end_encode(zse, 2);

    pouch_uplink_entry_write(SETTINGS_UPLINK_PATH,
                             POUCH_CONTENT_TYPE_CBOR,
                             buf,
                             zse->payload - buf,
                             K_FOREVER);
}

GOLIOTH_SERVICE(settings, SETTINGS_DOWNLINK_PATH, settings_downlink, settings_uplink);
