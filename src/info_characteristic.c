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

#include <cddl/info_encode.h>

#define INFO_CHRC_MAX_SIZE 64
#define DEVICE_ID "123456789"

static const struct bt_uuid_128 tf_info_chrc_uuid = BT_UUID_INIT_128(TF_UUID_GOLIOTH_INFO_CHRC_VAL);

static struct info_ctx
{
    uint8_t buf[INFO_CHRC_MAX_SIZE];
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

    /* Initialize packetizer and payload on first packet */

    if (NULL == ctx->packetizer)
    {
        /* Serialize Info CBOR */

        struct tf_info info = {
            .tf_info_enc_type_choice = tf_info_enc_type_plaintext_tstr_c,
            .tf_info_device_id =
                {
                    .value = DEVICE_ID,
                    .len = sizeof(DEVICE_ID) - 1,
                },
        };

        size_t info_len = 0;
        cbor_encode_tf_info(ctx->buf, INFO_CHRC_MAX_SIZE, &info, &info_len);

        ctx->packetizer = tf_packetizer_start(ctx->buf, info_len);
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

TOOTHFAIRY_CHARACTERISTIC(info,
                          (const struct bt_uuid *) &tf_info_chrc_uuid,
                          BT_GATT_CHRC_READ,
                          BT_GATT_PERM_READ,
                          info_read,
                          NULL,
                          &info_chrc_ctx);
