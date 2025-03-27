/*
 * Copyright (c) 2024 Golioth
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(info_chrc, LOG_LEVEL_DBG);

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

#include "golioth_ble_gatt_declarations.h"
#include "golioth_ble_gatt_uuids.h"
#include "packetizer.h"
#include "peripheral_int.h"

#include <cddl/info_encode.h>

#define INFO_CHRC_MAX_SIZE 64

static const struct bt_uuid_128 golioth_ble_gatt_info_chrc_uuid =
    BT_UUID_INIT_128(GOLIOTH_BLE_GATT_UUID_INFO_CHRC_VAL);

static struct info_ctx
{
    uint8_t buf[INFO_CHRC_MAX_SIZE];
    size_t buf_len;
    struct golioth_ble_gatt_packetizer *packetizer;
} info_chrc_ctx;

static ssize_t info_read(struct bt_conn *conn,
                         const struct bt_gatt_attr *attr,
                         void *buf,
                         uint16_t len,
                         uint16_t offset)
{
    /* Force packets into a single MTU */

    if (offset > 0)
    {
        return 0;
    }

    struct info_ctx *ctx = attr->user_data;

    /* Initialize packetizer on first packet */

    if (NULL == ctx->packetizer)
    {
        ctx->packetizer = golioth_ble_gatt_packetizer_start_buffer(ctx->buf, ctx->buf_len);
    }

    size_t buf_len = len;
    enum golioth_ble_gatt_packetizer_result res =
        golioth_ble_gatt_packetizer_get(ctx->packetizer, buf, &buf_len);

    if (GOLIOTH_BLE_GATT_PACKETIZER_ERROR == res)
    {
        int error = golioth_ble_gatt_packetizer_error(ctx->packetizer);
        LOG_ERR("Packetizer failed: %d", error);
        golioth_ble_gatt_packetizer_finish(ctx->packetizer);
        ctx->packetizer = NULL;
        return 0;
    }
    if (GOLIOTH_BLE_GATT_PACKETIZER_NO_MORE_DATA == res)
    {
        golioth_ble_gatt_packetizer_finish(ctx->packetizer);
        ctx->packetizer = NULL;
    }

    return buf_len;
}

static int info_init(const struct golioth_ble_gatt_peripheral *peripheral,
                     struct bt_gatt_attr *attr)
{
    struct info_ctx *ctx = attr->user_data;
    const char *device_id;

    golioth_ble_gatt_int_peripheral_get_device_id(peripheral, &device_id);

    struct golioth_ble_gatt_info info = {
        .golioth_ble_gatt_info_enc_type_choice = golioth_ble_gatt_info_enc_type_plaintext_tstr_c,
        .golioth_ble_gatt_info_device_id =
            {
                .value = device_id,
                .len = strlen(device_id),
            },
    };

    int err = cbor_encode_golioth_ble_gatt_info(ctx->buf, INFO_CHRC_MAX_SIZE, &info, &ctx->buf_len);

    return err;
}

GOLIOTH_BLE_GATT_CHARACTERISTIC(info,
                                (const struct bt_uuid *) &golioth_ble_gatt_info_chrc_uuid,
                                BT_GATT_CHRC_READ,
                                BT_GATT_PERM_READ,
                                info_init,
                                info_read,
                                NULL,
                                &info_chrc_ctx);
