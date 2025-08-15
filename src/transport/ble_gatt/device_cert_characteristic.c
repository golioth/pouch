/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

#include <pouch/types.h>
#include <pouch/transport/certificate.h>
#include <pouch/transport/ble_gatt/common/packetizer.h>
#include <pouch/transport/ble_gatt/common/uuids.h>

#include "golioth_ble_gatt_declarations.h"

static const struct bt_uuid_128 golioth_ble_gatt_device_cert_chrc_uuid =
    BT_UUID_INIT_128(GOLIOTH_BLE_GATT_UUID_DEVICE_CERT_CHRC_VAL);

static struct golioth_ble_gatt_device_cert_ctx
{
    struct golioth_ble_gatt_packetizer *packetizer;
    struct pouch_cert cert;
} device_cert_chrc_ctx;

static enum golioth_ble_gatt_packetizer_result device_cert_fill_cb(void *dst,
                                                                   size_t *dst_len,
                                                                   void *user_arg)
{
    struct pouch_cert *cert = user_arg;
    enum golioth_ble_gatt_packetizer_result ret = GOLIOTH_BLE_GATT_PACKETIZER_MORE_DATA;
    size_t maxlen = *dst_len;

    *dst_len = MIN(maxlen, cert->size);
    memcpy(dst, cert->buffer, *dst_len);

    cert->buffer += *dst_len;
    cert->size -= *dst_len;

    if (cert->size == 0)
    {
        ret = GOLIOTH_BLE_GATT_PACKETIZER_NO_MORE_DATA;
    }

    return ret;
}

static ssize_t device_cert_read(struct bt_conn *conn,
                                const struct bt_gatt_attr *attr,
                                void *buf,
                                uint16_t len,
                                uint16_t offset)
{
    /* Force packets into a single MTU */

    if (0 != offset)
    {
        return 0;
    }

    struct golioth_ble_gatt_device_cert_ctx *ctx = attr->user_data;

    if (NULL == ctx->packetizer)
    {
        int err;

        err = pouch_device_certificate_get(&ctx->cert);
        if (err)
        {
            return BT_GATT_ERR(BT_ATT_ERR_NOT_SUPPORTED);
        }

        ctx->packetizer =
            golioth_ble_gatt_packetizer_start_callback(device_cert_fill_cb, &ctx->cert);
    }

    size_t buf_len = len;
    enum golioth_ble_gatt_packetizer_result ret =
        golioth_ble_gatt_packetizer_get(ctx->packetizer, buf, &buf_len);

    if (GOLIOTH_BLE_GATT_PACKETIZER_ERROR == ret)
    {
        golioth_ble_gatt_packetizer_finish(ctx->packetizer);
        ctx->packetizer = NULL;
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }
    if (GOLIOTH_BLE_GATT_PACKETIZER_NO_MORE_DATA == ret)
    {
        golioth_ble_gatt_packetizer_finish(ctx->packetizer);
        ctx->packetizer = NULL;
    }

    return buf_len;
}


GOLIOTH_BLE_GATT_CHARACTERISTIC(device_cert,
                                (const struct bt_uuid *) &golioth_ble_gatt_device_cert_chrc_uuid,
                                BT_GATT_CHRC_READ,
                                BT_GATT_PERM_READ,
                                device_cert_read,
                                NULL,
                                &device_cert_chrc_ctx);
