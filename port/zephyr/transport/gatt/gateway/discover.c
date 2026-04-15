/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "discover.h"
#include "types.h"
#include "gateway.h"
#include "../common.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/sys/util.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(gatt_discover, CONFIG_POUCH_GATEWAY_GATT_LOG_LEVEL);

static const struct bt_uuid_128 golioth_svc_uuid_128 =
    BT_UUID_INIT_128(POUCH_GATT_UUID_SVC_VAL_128);
static const struct bt_uuid_16 golioth_svc_uuid_16 = BT_UUID_INIT_16(POUCH_GATT_UUID_SVC_VAL_16);
static const struct bt_uuid_128 char_uuids[POUCH_GATEWAY_GATT_ATTRS] = {
    [POUCH_GATEWAY_GATT_ATTR_INFO] = BT_UUID_INIT_128(POUCH_GATT_UUID_INFO_CHRC_VAL),
    [POUCH_GATEWAY_GATT_ATTR_DOWNLINK] = BT_UUID_INIT_128(POUCH_GATT_UUID_DOWNLINK_CHRC_VAL),
    [POUCH_GATEWAY_GATT_ATTR_UPLINK] = BT_UUID_INIT_128(POUCH_GATT_UUID_UPLINK_CHRC_VAL),
    [POUCH_GATEWAY_GATT_ATTR_SERVER_CERT] = BT_UUID_INIT_128(POUCH_GATT_UUID_SERVER_CERT_CHRC_VAL),
    [POUCH_GATEWAY_GATT_ATTR_DEVICE_CERT] = BT_UUID_INIT_128(POUCH_GATT_UUID_DEVICE_CERT_CHRC_VAL),
};
static const struct bt_uuid_16 gatt_ccc_uuid = BT_UUID_INIT_16(BT_UUID_GATT_CCC_VAL);

BUILD_ASSERT(ARRAY_SIZE(char_uuids) == POUCH_GATEWAY_GATT_ATTRS,
             "Missing characteristic UUID definitions");


static void complete(struct gatt_device *device, bool success)
{
    device->discover.callback(device, success);
}

static uint8_t discover_descriptors(struct bt_conn *conn,
                                    const struct bt_gatt_attr *attr,
                                    struct bt_gatt_discover_params *params)
{
    struct gatt_device *device = gateway_gatt_device(conn);
    if (attr)
    {
        int attr_idx = POUCH_GATEWAY_GATT_ATTRS;
        /* Find the value handle closest to but lower than this handle */
        for (int i = 0; i < POUCH_GATEWAY_GATT_ATTRS; i++)
        {
            if (device->chars[i].handle.value < attr->handle)
            {
                if (attr_idx == POUCH_GATEWAY_GATT_ATTRS)
                {
                    attr_idx = i;
                }
                else if (device->chars[i].handle.value > device->chars[attr_idx].handle.value)
                {
                    attr_idx = i;
                }
            }
        }

        if (attr_idx != POUCH_GATEWAY_GATT_ATTRS)
        {
            device->chars[attr_idx].handle.ccc = attr->handle;
            LOG_DBG("Found CCC descriptor handle %d for value handle %d",
                    device->chars[attr_idx].handle.ccc,
                    device->chars[attr_idx].handle.value);
        }

        return BT_GATT_ITER_CONTINUE;
    }

    complete(device, true);

    return BT_GATT_ITER_STOP;
}

static uint8_t discover_characteristics(struct bt_conn *conn,
                                        const struct bt_gatt_attr *attr,
                                        struct bt_gatt_discover_params *params)
{
    struct gatt_device *device = gateway_gatt_device(conn);
    if (attr)
    {
        struct bt_gatt_chrc *chrc = attr->user_data;
        for (int i = 0; i < POUCH_GATEWAY_GATT_ATTRS; i++)
        {
            if (0 == bt_uuid_cmp(chrc->uuid, &char_uuids[i].uuid))
            {
                device->chars[i].handle.value = chrc->value_handle;
                return BT_GATT_ITER_CONTINUE;
            }
        }

        LOG_WRN("Discovered Unknown characteristic: %d", chrc->value_handle);
        return BT_GATT_ITER_CONTINUE;
    }

    if (!device->chars[POUCH_GATEWAY_GATT_ATTR_UPLINK].handle.value
        || !device->chars[POUCH_GATEWAY_GATT_ATTR_DOWNLINK].handle.value)
    {
        LOG_ERR("Could not discover %s characteristics", "pouch");
        complete(device, false);
        return BT_GATT_ITER_STOP;
    }

    params->start_handle = params->end_handle;
    for (int i = 0; i < POUCH_GATEWAY_GATT_ATTRS; i++)
    {
        if (device->chars[i].handle.value < params->start_handle)
        {
            params->start_handle = device->chars[i].handle.value;
        }
    }

    /* Descriptors start after the value handle */
    params->start_handle += 1;
    params->func = discover_descriptors;
    params->type = BT_GATT_DISCOVER_DESCRIPTOR;
    params->uuid = &gatt_ccc_uuid.uuid;

    int err = bt_gatt_discover(conn, params);
    if (err)
    {
        LOG_ERR("Error discovering descriptors: %d", err);
        complete(device, false);
    }

    return BT_GATT_ITER_STOP;
}

static uint8_t discover_services(struct bt_conn *conn,
                                 const struct bt_gatt_attr *attr,
                                 struct bt_gatt_discover_params *params)
{
    struct gatt_device *device = gateway_gatt_device(conn);
    if (!attr)
    {
        if (params->uuid == &golioth_svc_uuid_16.uuid)
        {
            LOG_DBG("Could not find 16-bit UUID, beginning search for 128-bit");
            params->uuid = &golioth_svc_uuid_128.uuid;

            int err = bt_gatt_discover(conn, params);
            if (err)
            {
                LOG_ERR("Failed to start discovery: %d", err);
                complete(device, false);
            }
        }
        else
        {
            LOG_ERR("Missing pouch service");
            complete(device, false);
        }
        return BT_GATT_ITER_STOP;
    }

    struct bt_gatt_service_val *svc = attr->user_data;

    if (0 == bt_uuid_cmp(&golioth_svc_uuid_16.uuid, svc->uuid)
        || 0 == bt_uuid_cmp(&golioth_svc_uuid_128.uuid, svc->uuid))
    {
        params->func = discover_characteristics;
        params->type = BT_GATT_DISCOVER_CHARACTERISTIC;
        params->start_handle = attr->handle + 1;
        params->end_handle = svc->end_handle;
        params->uuid = NULL;

        int err = bt_gatt_discover(conn, params);
        if (err)
        {
            LOG_ERR("Error discovering characteristics: %d", err);
            complete(device, false);
        }

        return BT_GATT_ITER_STOP;
    }

    return BT_GATT_ITER_CONTINUE;
}

int gateway_bt_discover(struct gatt_device *device, gateway_gatt_discover_callback_t on_complete)
{
    struct bt_gatt_discover_params params = {
        .func = discover_services,
        .type = BT_GATT_DISCOVER_PRIMARY,
        .start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE,
        .end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE,
        .uuid = &golioth_svc_uuid_16.uuid,
    };
    device->discover.params = params;
    device->discover.callback = on_complete;

    return bt_gatt_discover(device->conn, &device->discover.params);
}
