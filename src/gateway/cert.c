/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <stdlib.h>

#include <mbedtls/x509_crt.h>

#include <pouch/gateway/cert.h>
#include <pouch/gateway/cloud.h>
#include <pouch/port.h>

POUCH_LOG_REGISTER(gw_cert, CONFIG_POUCH_GATEWAY_LOG_LEVEL);

static uint8_t server_crt_buf[CONFIG_POUCH_GATEWAY_SERVER_CERT_MAX_LEN];
static pouch_atomic_t server_crt_len;
static uint8_t server_crt_serial[CERT_SERIAL_MAXLEN];
static pouch_atomic_t server_crt_serial_len;
static pouch_atomic_t server_crt_id;

struct pouch_gateway_device_cert_context
{
    size_t len;
    uint8_t buf[CONFIG_POUCH_GATEWAY_DEVICE_CERT_MAX_LEN];
};

struct pouch_gateway_server_cert_context
{
    long id;
    size_t offset;
};

struct pouch_gateway_device_cert_context *pouch_gateway_device_cert_start(void)
{
    struct pouch_gateway_device_cert_context *context =
        malloc(sizeof(struct pouch_gateway_device_cert_context));

    if (context == NULL)
    {
        return NULL;
    }

    context->len = 0;

    return context;
}

int pouch_gateway_device_cert_push(struct pouch_gateway_device_cert_context *context,
                                   const void *data,
                                   size_t len)
{
    if (context->len + len > CONFIG_POUCH_GATEWAY_DEVICE_CERT_MAX_LEN)
    {
        return -ENOSPC;
    }

    memcpy(&context->buf[context->len], data, len);
    context->len += len;

    return 0;
}

void pouch_gateway_device_cert_abort(struct pouch_gateway_device_cert_context *context)
{
    free(context);
}

int pouch_gateway_device_cert_finish(struct pouch_gateway_device_cert_context *context)
{
    if (IS_ENABLED(CONFIG_POUCH_GATEWAY_CLOUD))
    {
        int err = pouch_gateway_cloud_upload_device_cert(context->buf, context->len);
        if (err)
        {
            POUCH_LOG_ERR("Failed to upload device cert to cloud: %d", err);
            pouch_gateway_device_cert_abort(context);
            return err;
        }
    }

    pouch_gateway_device_cert_abort(context);

    return 0;
}

struct pouch_gateway_server_cert_context *pouch_gateway_server_cert_start(void)
{
    struct pouch_gateway_server_cert_context *context =
        malloc(sizeof(struct pouch_gateway_server_cert_context));

    if (context == NULL)
    {
        return NULL;
    }

    context->id = pouch_atomic_get_value(&server_crt_id);
    context->offset = 0;

    return context;
}

bool pouch_gateway_server_cert_is_newest(const struct pouch_gateway_server_cert_context *context)
{
    return context->id == pouch_atomic_get_value(&server_crt_id);
}

static int server_crt_update(size_t len)
{
    mbedtls_x509_crt cert_chain;

    mbedtls_x509_crt_init(&cert_chain);

    int ret = mbedtls_x509_crt_parse(&cert_chain, server_crt_buf, len);
    if (ret < 0)
    {
        POUCH_LOG_ERR("Failed to parse certificate: 0x%x", -ret);
        mbedtls_x509_crt_free(&cert_chain);

        return -EIO;
    }

    POUCH_LOG_HEXDUMP(cert_chain.serial.p, cert_chain.serial.len, "cert_chain.serial");

    memcpy(server_crt_serial, cert_chain.serial.p, cert_chain.serial.len);
    pouch_atomic_set(&server_crt_serial_len, cert_chain.serial.len);

    pouch_atomic_set(&server_crt_len, len);
    pouch_atomic_inc(&server_crt_id);

    mbedtls_x509_crt_free(&cert_chain);

    return 0;
}

bool pouch_gateway_server_cert_is_complete(const struct pouch_gateway_server_cert_context *context)
{
    return context->offset >= pouch_atomic_get_value(&server_crt_len);
}

int pouch_gateway_server_cert_get_data(struct pouch_gateway_server_cert_context *context,
                                       void *dst,
                                       size_t *dst_len,
                                       bool *is_last)
{
    size_t len = pouch_atomic_get_value(&server_crt_len);

    *is_last = false;

    if (context->offset >= len)
    {
        *dst_len = 0;
        return -ENODATA;
    }

    if (*dst_len > len - context->offset)
    {
        *dst_len = len - context->offset;
    }

    memcpy(dst, &server_crt_buf[context->offset], *dst_len);
    context->offset += *dst_len;

    if (context->offset >= len)
    {
        *is_last = true;
    }

    return 0;
}

void pouch_gateway_server_cert_get_serial(void *dst, size_t *dst_len)
{
    size_t len = pouch_atomic_get_value(&server_crt_serial_len);

    if (*dst_len > len)
    {
        *dst_len = len;
    }

    memcpy(dst, server_crt_serial, *dst_len);
}

void pouch_gateway_server_cert_abort(struct pouch_gateway_server_cert_context *context)
{
    free(context);
}

void pouch_gateway_cert_module_init(void)
{
#if defined(CONFIG_POUCH_GATEWAY_SERVER_CERT_BUILTIN)
    static const uint8_t server_crt_offline[] = {
#include "pouch_gateway_server.pem.inc"
    };

    memcpy(server_crt_buf, server_crt_offline, sizeof(server_crt_offline));
    server_crt_update(sizeof(server_crt_offline));

    POUCH_LOG_INF("Loaded builtin server cert");
#endif

    POUCH_LOG_HEXDUMP(server_crt_buf,
                      pouch_atomic_get_value(&server_crt_len),
                      "Server certificate");
}

int pouch_gateway_server_cert_set(const uint8_t *cert, size_t len)
{
    if (len > sizeof(server_crt_buf))
    {
        POUCH_LOG_ERR("Server cert too large (%zu > %zu)", len, sizeof(server_crt_buf));
        return -ENOSPC;
    }

    memcpy(server_crt_buf, cert, len);

    int err = server_crt_update(len);
    if (err)
    {
        POUCH_LOG_ERR("Failed to update server cert: %d", err);
        return err;
    }

    POUCH_LOG_INF("Server cert updated (%zu bytes)", len);
    return 0;
}
