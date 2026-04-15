/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>

#include <pouch/transport/bluetooth/gatt.h>

#include <pouch/gateway/bt/bond.h>
#include "scan.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(custom_scan, LOG_LEVEL_DBG);

static inline bool version_is_compatible(const struct pouch_gatt_adv_data *adv_data)
{
    uint8_t self_ver =
        adv_data->version & POUCH_GATT_ADV_VERSION_SELF_MASK >> POUCH_GATT_ADV_VERSION_SELF_SHIFT;

    return POUCH_GATT_VERSION == self_ver;
}

static inline bool sync_requested(const struct pouch_gatt_adv_data *adv_data)
{
    return (adv_data->flags & POUCH_GATT_ADV_FLAG_SYNC_REQUEST);
}

struct tf_data
{
    const bt_addr_le_t *addr;
    bool is_tf;
    bool is_bonded;
    bool name_is_golioth;
    struct pouch_gatt_adv_data adv_data;
};

static const struct bt_uuid_16 golioth_svc_uuid_16 = BT_UUID_INIT_16(POUCH_GATT_UUID_SVC_VAL_16);

static bool data_cb(struct bt_data *data, void *user_data)
{
    struct tf_data *tf = user_data;

    switch (data->type)
    {
        case BT_DATA_NAME_COMPLETE:
        {
            if (!strncmp(data->data, "Golioth", data->data_len))
            {
                tf->name_is_golioth = true;
            }

            return true;
        }

        case BT_DATA_SVC_DATA16:
        {
            const struct pouch_gatt_adv_data *adv_data;

            if (data->data_len >= sizeof(golioth_svc_uuid_16.val) + sizeof(*adv_data)
                && memcmp(&golioth_svc_uuid_16.val, data->data, sizeof(golioth_svc_uuid_16.val))
                    == 0)
            {
                adv_data = (const void *) &data->data[sizeof(golioth_svc_uuid_16.val)];

                tf->is_tf = true;
                tf->adv_data = *adv_data;

                return true;
            }
            return true;
        }

        default:
            return true;
    }
}

static void bond_filter(const struct bt_bond_info *info, void *user_data)
{
    struct tf_data *tf = user_data;

    if (memcmp(&info->addr, tf->addr, sizeof(info->addr)) == 0)
    {
        tf->is_bonded = true;
    }
}

static void device_found(const bt_addr_le_t *addr,
                         int8_t rssi,
                         uint8_t type,
                         struct net_buf_simple *ad)
{
    char addr_str[BT_ADDR_LE_STR_LEN];
    struct tf_data tf = {
        .is_tf = false,
    };
    int err;

    /* We're only interested in connectable events */
    if (type != BT_GAP_ADV_TYPE_ADV_IND && type != BT_GAP_ADV_TYPE_ADV_DIRECT_IND
        && type != BT_GAP_ADV_TYPE_SCAN_RSP)
    {
        return;
    }

    bt_data_parse(ad, data_cb, &tf);

    if (!tf.is_tf)
    {
        return;
    }

    bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));

    if (IS_ENABLED(CONFIG_POUCH_GATEWAY_GATT_SCAN_FILTER_BONDED))
    {
        bt_foreach_bond(BT_ID_DEFAULT, bond_filter, &tf);

        LOG_DBG("Pouch device found: %s, (RSSI %d) (bonded %d)",
                addr_str,
                rssi,
                (int) tf.is_bonded);
    }
    else
    {
        LOG_DBG("Pouch device found: %s, (RSSI %d)", addr_str, rssi);
    }

    LOG_DBG("version=0x%0x flags=0%0x name_is_golioth=%d rssi=%d",
            tf.adv_data.version,
            tf.adv_data.flags,
            tf.name_is_golioth,
            rssi);

    if (!version_is_compatible(&tf.adv_data) || !tf.name_is_golioth || rssi <= -70)
    {
        return;
    }

    if (!tf.is_bonded && !pouch_gateway_bonding_is_enabled())
    {
        return;
    }

    if (tf.is_bonded && !sync_requested(&tf.adv_data))
    {
        return;
    }

    err = bt_le_scan_stop();
    if (err)
    {
        LOG_ERR("Failed to stop scanning");
        return;
    }

    struct bt_conn *conn = NULL;
    err = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN, BT_LE_CONN_PARAM_DEFAULT, &conn);
    if (err)
    {
        LOG_ERR("Create auto conn failed (%d)", err);
        custom_scan_start();
        return;
    }

    /* Disable bonding after first connect attempt */
    if (!tf.is_bonded)
    {
        pouch_gateway_bonding_disable();
    }
}

void custom_scan_start(void)
{
    int err;

    err = bt_le_scan_start(BT_LE_SCAN_PARAM(BT_LE_SCAN_TYPE_ACTIVE,
                                            BT_LE_SCAN_OPT_NONE,
                                            BT_GAP_SCAN_FAST_INTERVAL_MIN,
                                            BT_GAP_SCAN_FAST_WINDOW),
                           device_found);
    if (err)
    {
        LOG_ERR("Scanning failed to start (err %d)", err);
        return;
    }

    LOG_INF("Scanning successfully started");
}
