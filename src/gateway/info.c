/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <pouch/gateway/cert.h>
#include <pouch/gateway/info.h>

#include <cddl/info_decode.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(info, CONFIG_POUCH_GATEWAY_LOG_LEVEL);

#define INFO_MAX_SIZE 64

#define INFO_FLAG_DEVICE_PROVISIONED BIT(0)

struct pouch_gateway_info_context
{
    size_t len;
    uint8_t buf[INFO_MAX_SIZE];
};

struct pouch_gateway_info_context *pouch_gateway_info_start(void)
{
    struct pouch_gateway_info_context *context = malloc(sizeof(struct pouch_gateway_info_context));

    if (context == NULL)
    {
        return NULL;
    }

    context->len = 0;

    return context;
}

int pouch_gateway_info_push(struct pouch_gateway_info_context *context,
                            const void *data,
                            size_t len)
{
    if (context->len + len > INFO_MAX_SIZE)
    {
        return -ENOSPC;
    }

    memcpy(&context->buf[context->len], data, len);
    context->len += len;

    return 0;
}

void pouch_gateway_info_abort(struct pouch_gateway_info_context *context)
{
    free(context);
}

int pouch_gateway_info_finish(struct pouch_gateway_info_context *context,
                              bool *server_cert_provisioned,
                              bool *device_cert_provisioned)
{
    struct pouch_gatt_info info;
    uint8_t server_cert_serial_buf[CERT_SERIAL_MAXLEN];
    struct zcbor_string server_cert_serial = {
        .value = server_cert_serial_buf,
        .len = sizeof(server_cert_serial_buf),
    };
    int err;

    err = cbor_decode_pouch_gatt_info(context->buf, context->len, &info, NULL);
    if (err)
    {
        LOG_ERR("Failed to parse info: %d", err);
        pouch_gateway_info_abort(context);
        return -EIO;
    }

    pouch_gateway_server_cert_get_serial(server_cert_serial_buf, &server_cert_serial.len);

    *server_cert_provisioned = (zcbor_compare_strings(&info.server_cert_snr, &server_cert_serial));
    *device_cert_provisioned = !!(info.flags & INFO_FLAG_DEVICE_PROVISIONED);

    pouch_gateway_info_abort(context);

    return 0;
}
