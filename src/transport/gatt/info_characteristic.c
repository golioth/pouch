/*
 * Copyright (c) 2024 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(info_chrc, CONFIG_POUCH_LOG_LEVEL);

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

#include <pouch/transport/gatt/common/packetizer.h>
#include <pouch/transport/gatt/common/uuids.h>
#include <pouch/transport/certificate.h>
#include <pouch/certificate.h>

#include "pouch_gatt_declarations.h"

#include <cddl/info_encode.h>

#define INFO_CHRC_MAX_SIZE 64

enum info_flags
{
    INFO_FLAG_PROVISIONED = BIT(0),
};

static const struct bt_uuid_128 pouch_gatt_info_chrc_uuid =
    BT_UUID_INIT_128(POUCH_GATT_UUID_INFO_CHRC_VAL);

static struct info_ctx
{
    uint8_t buf[INFO_CHRC_MAX_SIZE];
    size_t buf_len;
    struct pouch_gatt_packetizer *packetizer;
} info_chrc_ctx;

static int build_info_data(void)
{
#if CONFIG_POUCH_ENCRYPTION_SAEAD
    uint8_t snr[CERT_SERIAL_MAXLEN];
    ssize_t snr_len = pouch_server_certificate_serial_get(snr, sizeof(snr));
    if (snr_len < 0)
    {
        return snr_len;
    }
#else
    uint8_t *snr = NULL;
    ssize_t snr_len = 0;
#endif

    // TODO: Set the Provisioned flag once we have the functionality to know whether provisioning
    // has taken place.
    enum info_flags flags = 0;

    struct pouch_gatt_info info = {
        .pouch_gatt_info_flags = flags,
        .pouch_gatt_info_server_cert_snr =
            {
                .value = snr,
                .len = snr_len,
            },
    };

    info_chrc_ctx.buf_len = INFO_CHRC_MAX_SIZE;

    return cbor_encode_pouch_gatt_info(info_chrc_ctx.buf,
                                       INFO_CHRC_MAX_SIZE,
                                       &info,
                                       &info_chrc_ctx.buf_len);
}

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
        int err = build_info_data();
        if (err)
        {
            return err;
        }

        ctx->packetizer = pouch_gatt_packetizer_start_buffer(ctx->buf, ctx->buf_len);
    }

    size_t buf_len = len;
    enum pouch_gatt_packetizer_result res =
        pouch_gatt_packetizer_get(ctx->packetizer, buf, &buf_len);

    if (POUCH_GATT_PACKETIZER_ERROR == res)
    {
        int error = pouch_gatt_packetizer_error(ctx->packetizer);
        LOG_ERR("Packetizer failed: %d", error);
        pouch_gatt_packetizer_finish(ctx->packetizer);
        ctx->packetizer = NULL;
        return 0;
    }
    if (POUCH_GATT_PACKETIZER_NO_MORE_DATA == res)
    {
        pouch_gatt_packetizer_finish(ctx->packetizer);
        ctx->packetizer = NULL;
    }

    return buf_len;
}

POUCH_GATT_CHARACTERISTIC(info,
                          (const struct bt_uuid *) &pouch_gatt_info_chrc_uuid,
                          BT_GATT_CHRC_READ,
                          BT_GATT_PERM_READ,
                          info_read,
                          NULL,
                          &info_chrc_ctx);
