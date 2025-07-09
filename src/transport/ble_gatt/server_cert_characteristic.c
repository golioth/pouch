/*
 * Copyright (c) 2025 Golioth
 */

#include <stdlib.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

#include <pouch/types.h>
#include <pouch/certificate.h>
#include <pouch/transport/certificate.h>
#include <pouch/transport/ble_gatt/common/packetizer.h>
#include <pouch/transport/ble_gatt/common/uuids.h>

#include "golioth_ble_gatt_declarations.h"

static const struct bt_uuid_128 golioth_ble_gatt_server_cert_chrc_uuid =
    BT_UUID_INIT_128(GOLIOTH_BLE_GATT_UUID_SERVER_CERT_CHRC_VAL);

static struct golioth_ble_gatt_server_cert_ctx
{
    struct pouch_cert cert;
} server_cert_chrc_ctx;

static ssize_t server_cert_serial_read(struct bt_conn *conn,
                                       const struct bt_gatt_attr *attr,
                                       void *buf,
                                       uint16_t len,
                                       uint16_t offset)
{
    uint8_t empty[] = {0x03};

    return bt_gatt_attr_read(conn, attr, buf, len, offset, empty, sizeof(empty));
}

static ssize_t server_cert_write(struct bt_conn *conn,
                                 const struct bt_gatt_attr *attr,
                                 const void *buf,
                                 uint16_t len,
                                 uint16_t offset,
                                 uint8_t flags)
{
    struct golioth_ble_gatt_server_cert_ctx *ctx = attr->user_data;
    bool is_first = false;
    bool is_last = false;
    const void *payload = NULL;
    ssize_t payload_len =
        golioth_ble_gatt_packetizer_decode(buf, len, &payload, &is_first, &is_last);

    if (0 >= payload_len)
    {
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }

    if (is_first)
    {
        free((void *) ctx->cert.der);
        ctx->cert.der = malloc(CONFIG_POUCH_SERVER_CERT_MAX_LEN);

        ctx->cert.size = 0;
    }

    if (!ctx->cert.der)
    {
        return BT_GATT_ERR(BT_ATT_ERR_INSUFFICIENT_RESOURCES);
    }

    memcpy((void *) &ctx->cert.der[ctx->cert.size], payload, payload_len);
    ctx->cert.size += payload_len;

    if (is_last)
    {
        pouch_server_certificate_set(&ctx->cert);

        free((void *) ctx->cert.der);
        ctx->cert.der = NULL;
    }

    return len;
}

GOLIOTH_BLE_GATT_CHARACTERISTIC(server_cert,
                                (const struct bt_uuid *) &golioth_ble_gatt_server_cert_chrc_uuid,
                                BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
                                BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                                NULL,
                                server_cert_serial_read,
                                server_cert_write,
                                &server_cert_chrc_ctx);
