/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

#include <pouch/types.h>
#include <pouch/certificate.h>
#include <pouch/transport/certificate.h>
#include <pouch/transport/gatt/common/packetizer.h>
#include <pouch/transport/gatt/common/uuids.h>

#include "pouch_gatt_declarations.h"

static const struct bt_uuid_128 pouch_gatt_server_cert_chrc_uuid =
    BT_UUID_INIT_128(POUCH_GATT_UUID_SERVER_CERT_CHRC_VAL);

static struct pouch_gatt_server_cert_ctx
{
    struct pouch_gatt_packetizer *packetizer;
    struct pouch_cert cert;
    uint8_t serial[CERT_SERIAL_MAXLEN];
    uint8_t serial_len;
    uint8_t serial_offset;
} server_cert_chrc_ctx;

static enum pouch_gatt_packetizer_result server_cert_serial_fill_cb(void *dst,
                                                                    size_t *dst_len,
                                                                    void *user_arg)
{
    struct pouch_gatt_server_cert_ctx *ctx = user_arg;
    enum pouch_gatt_packetizer_result ret = POUCH_GATT_PACKETIZER_MORE_DATA;
    size_t maxlen = *dst_len;

    *dst_len = MIN(maxlen, ctx->serial_len - ctx->serial_offset);
    memcpy(dst, &ctx->serial[ctx->serial_offset], *dst_len);

    ctx->serial_offset += *dst_len;

    if (ctx->serial_offset >= ctx->serial_len)
    {
        ret = POUCH_GATT_PACKETIZER_NO_MORE_DATA;
    }

    return ret;
}

static ssize_t server_cert_serial_read(struct bt_conn *conn,
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

    struct pouch_gatt_server_cert_ctx *ctx = attr->user_data;

    if (NULL == ctx->packetizer)
    {
        int ret;

        ret = pouch_server_certificate_serial_get(ctx->serial, sizeof(ctx->serial));
        if (ret < 0)
        {
            return BT_GATT_ERR(BT_ATT_ERR_NOT_SUPPORTED);
        }

        ctx->serial_len = ret;
        ctx->serial_offset = 0;

        ctx->packetizer = pouch_gatt_packetizer_start_callback(server_cert_serial_fill_cb, ctx);
    }

    size_t buf_len = len;
    enum pouch_gatt_packetizer_result ret =
        pouch_gatt_packetizer_get(ctx->packetizer, buf, &buf_len);

    if (POUCH_GATT_PACKETIZER_ERROR == ret)
    {
        pouch_gatt_packetizer_finish(ctx->packetizer);
        ctx->packetizer = NULL;
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }
    if (POUCH_GATT_PACKETIZER_NO_MORE_DATA == ret)
    {
        pouch_gatt_packetizer_finish(ctx->packetizer);
        ctx->packetizer = NULL;
    }

    return buf_len;
}

static ssize_t server_cert_write(struct bt_conn *conn,
                                 const struct bt_gatt_attr *attr,
                                 const void *buf,
                                 uint16_t len,
                                 uint16_t offset,
                                 uint8_t flags)
{
    struct pouch_gatt_server_cert_ctx *ctx = attr->user_data;
    bool is_first = false;
    bool is_last = false;
    const void *payload = NULL;
    ssize_t payload_len = pouch_gatt_packetizer_decode(buf, len, &payload, &is_first, &is_last);

    if (0 >= payload_len)
    {
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }

    if (is_first)
    {
        free((void *) ctx->cert.buffer);
        ctx->cert.buffer = malloc(CONFIG_POUCH_SERVER_CERT_MAX_LEN);

        ctx->cert.size = 0;
    }

    if (!ctx->cert.buffer)
    {
        return BT_GATT_ERR(BT_ATT_ERR_INSUFFICIENT_RESOURCES);
    }

    memcpy((void *) &ctx->cert.buffer[ctx->cert.size], payload, payload_len);
    ctx->cert.size += payload_len;

    if (is_last)
    {
        int err = pouch_server_certificate_set(&ctx->cert);
        if (err)
        {
            bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
        }

        free((void *) ctx->cert.buffer);
        ctx->cert.buffer = NULL;
    }

    return len;
}

POUCH_GATT_CHARACTERISTIC(server_cert,
                          (const struct bt_uuid *) &pouch_gatt_server_cert_chrc_uuid,
                          BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
                          BT_GATT_PERM_READ_LESC | BT_GATT_PERM_WRITE_LESC,
                          server_cert_serial_read,
                          server_cert_write,
                          &server_cert_chrc_ctx);
