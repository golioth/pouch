/*
 * Copyright (c) 2024 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

#include <pouch/transport/gatt/common/packetizer.h>
#include <pouch/transport/gatt/common/sender.h>
#include <pouch/transport/gatt/common/uuids.h>
#include <pouch/transport/certificate.h>
#include <pouch/certificate.h>

#include "pouch_gatt_declarations.h"

#include <cddl/info_encode.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(info_chrc, CONFIG_POUCH_GATT_LOG_LEVEL);

#define BT_ATT_OVERHEAD 3

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
    struct pouch_gatt_sender *sender;
} info_chrc_ctx;

struct bt_gatt_attr info_chrc;

static int build_info_data(struct info_ctx *ctx)
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

    ctx->buf_len = INFO_CHRC_MAX_SIZE;

    return cbor_encode_pouch_gatt_info(ctx->buf, INFO_CHRC_MAX_SIZE, &info, &ctx->buf_len);
}

static void info_cleanup(struct info_ctx *ctx)
{
    if (NULL != ctx->packetizer)
    {
        pouch_gatt_packetizer_finish(ctx->packetizer);
        ctx->packetizer = NULL;
    }

    if (NULL != ctx->sender)
    {
        pouch_gatt_sender_destroy(ctx->sender);
        ctx->sender = NULL;
    }
}

static int send_info_data(void *conn, const void *data, size_t length)
{
    return bt_gatt_notify(conn, &info_chrc, data, length);
}

static ssize_t info_write(struct bt_conn *conn,
                          const struct bt_gatt_attr *attr,
                          const void *buf,
                          uint16_t len,
                          uint16_t offset,
                          uint8_t flags)
{
    struct info_ctx *ctx = attr->user_data;

    if (NULL == ctx->sender)
    {
        if (pouch_gatt_packetizer_is_ack(buf, len))
        {
            LOG_DBG("Received ACK while idle");

            pouch_gatt_sender_send_fin(send_info_data, conn, POUCH_GATT_NACK_IDLE);
        }
        else
        {
            /* Received NACK (or malformed packet) while idle. Do nothing */
            LOG_WRN("Received NACK while idle");
        }

        return len;
    }

    bool complete = false;
    int ret = pouch_gatt_sender_receive_ack(ctx->sender, buf, len, &complete);
    if (0 > ret)
    {
        LOG_ERR("Error handling ACK: %d", ret);

        info_cleanup(ctx);

        bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);

        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }

    if (0 < ret)
    {
        LOG_WRN("Received NACK: %d", ret);

        info_cleanup(ctx);

        bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    }

    if (complete)
    {
        LOG_DBG("Info send complete");

        info_cleanup(ctx);
    }

    return len;
}

POUCH_GATT_CHARACTERISTIC(info,
                          (const struct bt_uuid *) &pouch_gatt_info_chrc_uuid,
                          BT_GATT_CHRC_NOTIFY | BT_GATT_CHRC_WRITE,
                          POUCH_GATT_PERM_WRITE,
                          NULL,
                          info_write,
                          &info_chrc_ctx);

static ssize_t info_ccc_write(struct bt_conn *conn, const struct bt_gatt_attr *attr, uint16_t value)
{
    struct info_ctx *ctx = &info_chrc_ctx;

    if (value & BT_GATT_CCC_NOTIFY)
    {
        uint16_t mtu = bt_gatt_get_mtu(conn);
        if (mtu <= BT_ATT_OVERHEAD)
        {
            LOG_ERR("Invalid MTU, likely disconnected");
            bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
            return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
        }
        mtu -= BT_ATT_OVERHEAD;

        int err = build_info_data(ctx);
        if (err)
        {
            LOG_ERR("Failed to build info payload");
            bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
            return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
        }

        ctx->packetizer = pouch_gatt_packetizer_start_buffer(ctx->buf, ctx->buf_len);
        if (NULL == ctx->packetizer)
        {
            LOG_ERR("Could not create packetizer");
            bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
            return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
        }

        ctx->sender = pouch_gatt_sender_create(ctx->packetizer, send_info_data, conn, mtu);
        if (NULL == ctx->sender)
        {
            LOG_ERR("Could not create sender");
            info_cleanup(ctx);
            bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
            return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
        }
    }

    return sizeof(value);
}

static void info_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    if (value & BT_GATT_CCC_NOTIFY)
    {
        LOG_DBG("Notifications enabled");
    }
    else
    {
        LOG_DBG("Notifications disabled");
        info_cleanup(&info_chrc_ctx);
    }
}

POUCH_GATT_CCC(info, info_ccc_changed, info_ccc_write, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE);
