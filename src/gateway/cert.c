/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <stdlib.h>

#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>

#include <pouch/gateway/cert.h>

#include <golioth/gateway.h>
#include <golioth/golioth_status.h>

#include <zephyr/sys/atomic_types.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(cert, CONFIG_POUCH_GATEWAY_LOG_LEVEL);

static struct golioth_client *_client;

static uint8_t server_crt_buf[CONFIG_POUCH_GATEWAY_SERVER_CERT_MAX_LEN];
static atomic_t server_crt_len;
static uint8_t server_crt_serial[CERT_SERIAL_MAXLEN];
static atomic_t server_crt_serial_len;
static atomic_t server_crt_id;

struct pouch_gateway_device_cert_context
{
    size_t len;
    uint8_t buf[CONFIG_POUCH_GATEWAY_DEVICE_CERT_MAX_LEN];
};

struct pouch_gateway_server_cert_context
{
    atomic_val_t id;
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

struct device_cert_set_ctx
{
    struct k_sem sem;
    enum golioth_status status;
};

static void device_cert_set_callback(struct golioth_client *client,
                                     enum golioth_status status,
                                     const struct golioth_coap_rsp_code *coap_rsp_code,
                                     const char *path,
                                     void *arg)
{
    struct device_cert_set_ctx *ctx = arg;

    ctx->status = status;
    k_sem_give(&ctx->sem);
}

int pouch_gateway_device_cert_finish(struct pouch_gateway_device_cert_context *context)
{
    struct device_cert_set_ctx ctx = {};
    enum golioth_status status;

    k_sem_init(&ctx.sem, 0, 1);

    if (IS_ENABLED(CONFIG_POUCH_GATEWAY_CLOUD))
    {
        status = golioth_gateway_device_cert_set(_client,
                                                 context->buf,
                                                 context->len,
                                                 device_cert_set_callback,
                                                 &ctx,
                                                 5);
        if (status != GOLIOTH_OK)
        {
            LOG_ERR("Failed to finish device cert: %d", status);
            return -EIO;
        }

        k_sem_take(&ctx.sem, K_FOREVER);

        if (ctx.status != GOLIOTH_OK)
        {
            LOG_ERR("Failed to set cert: %d", ctx.status);
            return -EIO;
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

    context->id = atomic_get(&server_crt_id);
    context->offset = 0;

    return context;
}

bool pouch_gateway_server_cert_is_newest(const struct pouch_gateway_server_cert_context *context)
{
    return context->id == atomic_get(&server_crt_id);
}

static int server_crt_update(size_t len)
{
    mbedtls_x509_crt cert_chain;

    mbedtls_x509_crt_init(&cert_chain);

    int ret = mbedtls_x509_crt_parse(&cert_chain, server_crt_buf, len);
    if (ret < 0)
    {
        LOG_ERR("Failed to parse certificate: 0x%x", -ret);
        mbedtls_x509_crt_free(&cert_chain);

        return -EIO;
    }

    LOG_HEXDUMP_DBG(cert_chain.serial.p, cert_chain.serial.len, "cert_chain.serial");

    memcpy(server_crt_serial, cert_chain.serial.p, cert_chain.serial.len);
    atomic_set(&server_crt_serial_len, cert_chain.serial.len);

    atomic_set(&server_crt_len, len);
    atomic_inc(&server_crt_id);

    mbedtls_x509_crt_free(&cert_chain);

    return 0;
}

bool pouch_gateway_server_cert_is_complete(const struct pouch_gateway_server_cert_context *context)
{
    return context->offset >= atomic_get(&server_crt_len);
}

int pouch_gateway_server_cert_get_data(struct pouch_gateway_server_cert_context *context,
                                       void *dst,
                                       size_t *dst_len,
                                       bool *is_last)
{
    size_t len = atomic_get(&server_crt_len);

    *is_last = false;

    if (context->offset >= len)
    {
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
    size_t len = atomic_get(&server_crt_serial_len);

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

void pouch_gateway_cert_module_on_connected(struct golioth_client *client)
{
    enum golioth_status status;

    _client = client;

    if (IS_ENABLED(CONFIG_POUCH_GATEWAY_CLOUD))
    {
        size_t len = sizeof(server_crt_buf);
        status = golioth_gateway_server_cert_get(client, server_crt_buf, &len);
        if (status != GOLIOTH_OK)
        {
            LOG_ERR("Failed to download server certificate: %d", status);
            return;
        }

        server_crt_update(len);
    }
    else if (IS_ENABLED(CONFIG_POUCH_GATEWAY_SERVER_CERT_BUILTIN))
    {
        static const uint8_t server_crt_offline[] = {
#include "pouch_gateway_server.pem.inc"
        };

        memcpy(server_crt_buf, server_crt_offline, sizeof(server_crt_offline));
        server_crt_update(sizeof(server_crt_offline));

        LOG_INF("Loaded builtin server cert");
    }

    LOG_HEXDUMP_DBG(server_crt_buf, atomic_get(&server_crt_len), "Server certificate");
}
