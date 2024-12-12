/*
 * Copyright (c) 2024 Golioth
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(info_chrc, LOG_LEVEL_DBG);

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

#include "toothfairy_declarations.h"
#include "toothfairy_uuids.h"
#include "packetizer.h"
#include "peripheral_int.h"

#include <cddl/info_encode.h>

#define INFO_CHRC_MAX_SIZE 64

static const struct bt_uuid_128 tf_info_chrc_uuid = BT_UUID_INIT_128(TF_UUID_GOLIOTH_INFO_CHRC_VAL);

static struct info_ctx
{
    uint8_t buf[INFO_CHRC_MAX_SIZE];
    size_t buf_len;
    struct tf_packetizer *packetizer;
} info_chrc_ctx;

static ssize_t info_read(struct bt_conn *conn,
                         const struct bt_gatt_attr *attr,
                         void *buf,
                         uint16_t len,
                         uint16_t offset)
{
    /* Force TF packets into a single MTU */

    if (offset > 0)
    {
        return 0;
    }

    struct info_ctx *ctx = attr->user_data;

    /* Initialize packetizer on first packet */

    if (NULL == ctx->packetizer)
    {
        ctx->packetizer = tf_packetizer_start(ctx->buf, ctx->buf_len);
    }

    size_t buf_len = len;
    enum tf_packetizer_result res = tf_packetizer_get(ctx->packetizer, buf, &buf_len);

    if (TF_PACKETIZER_ERROR == res)
    {
        int error = tf_packetizer_error(ctx->packetizer);
        LOG_ERR("Toothfairy packetizer failed: %d", error);
        tf_packetizer_finish(ctx->packetizer);
        ctx->packetizer = NULL;
        return 0;
    }
    if (TF_PACKETIZER_NO_MORE_DATA == res)
    {
        tf_packetizer_finish(ctx->packetizer);
        ctx->packetizer = NULL;
    }

    return buf_len;
}

static int info_init(const struct toothfairy_peripheral *tf_peripheral, struct bt_gatt_attr *attr)
{
    struct info_ctx *ctx = attr->user_data;
    const char *device_id;

    tf_int_peripheral_get_device_id(tf_peripheral, &device_id);

    struct tf_info info = {
        .tf_info_enc_type_choice = tf_info_enc_type_plaintext_tstr_c,
        .tf_info_device_id =
            {
                .value = device_id,
                .len = strlen(device_id),
            },
    };

    int err = cbor_encode_tf_info(ctx->buf, INFO_CHRC_MAX_SIZE, &info, &ctx->buf_len);

    return err;
}

TOOTHFAIRY_CHARACTERISTIC(info,
                          (const struct bt_uuid *) &tf_info_chrc_uuid,
                          BT_GATT_CHRC_READ,
                          BT_GATT_PERM_READ,
                          info_init,
                          info_read,
                          NULL,
                          &info_chrc_ctx);
