/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Pouch CoAP cloud transport for the gateway.
 *
 * Implements the @ref pouch_gateway_cloud_transport vtable defined in
 * <pouch/gateway/cloud.h> using the connection helpers exposed by
 * client.c via coap_internal.h.
 *
 * Registered with the gateway core by calling
 * @ref pouch_coap_gateway_init() from application code.
 */

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <pouch/gateway/cert.h>
#include <pouch/gateway/cloud.h>
#include <pouch/transport/coap/gateway.h>

#include "coap_internal.h"

LOG_MODULE_REGISTER(pouch_coap_gateway, CONFIG_POUCH_COAP_CLIENT_LOG_LEVEL);

#define COAP_PATH_POUCH "/.g/pouch"
#define COAP_PATH_DEVICE_CERT "/.g/device-cert"

/*--------------------------------------------------
 * Cert propagation: push the cached server cert to the gateway core
 *------------------------------------------------*/

/*
 * Push the cached server certificate to the gateway core so the
 * gateway can hand it out to BLE peripherals.  Safe to call multiple
 * times; the gateway core deduplicates internally via a serial check.
 */
static void publish_server_cert_to_gateway(void)
{
    const uint8_t *buf = NULL;
    size_t len = pouch_coap_server_cert_get(&buf);

    if (len == 0 || buf == NULL)
    {
        return;
    }

    int err = pouch_gateway_server_cert_set(buf, len);
    if (err)
    {
        LOG_WRN("Failed to publish server cert to gateway: %d", err);
    }
}

/*--------------------------------------------------
 * Vtable ops
 *------------------------------------------------*/

static int coap_ensure_ready(void)
{
    int err;

    k_mutex_lock(&pouch_coap_mutex, K_FOREVER);

    err = pouch_coap_setup_socket();
    if (err)
    {
        goto cleanup;
    }

    err = pouch_coap_fetch_server_cert();
    if (err)
    {
        goto cleanup;
    }

    err = pouch_coap_upload_cert();
    if (err)
    {
        goto cleanup;
    }

    publish_server_cert_to_gateway();
    k_mutex_unlock(&pouch_coap_mutex);
    return 0;

cleanup:
    pouch_coap_close_connection();
    k_mutex_unlock(&pouch_coap_mutex);
    return err;
}

struct forward_pouch_ctx
{
    pouch_gateway_cloud_block2_cb_t resp_cb;
    void *user_data;
};

static int forward_pouch_block2_wrapper(const uint8_t *data,
                                        size_t len,
                                        bool first_block,
                                        bool last_block,
                                        void *user_data)
{
    struct forward_pouch_ctx *ctx = user_data;

    (void) first_block;

    if (ctx->resp_cb == NULL)
    {
        return 0;
    }

    return ctx->resp_cb(data, len, last_block, ctx->user_data);
}

struct forward_pouch_chunk_ctx
{
    pouch_gateway_cloud_upload_chunk_cb_t chunk_cb;
    void *user_data;
};

static int forward_pouch_chunk_wrapper(uint8_t *buf,
                                       size_t buf_size,
                                       size_t *chunk_len,
                                       bool *is_last,
                                       void *user_data)
{
    struct forward_pouch_chunk_ctx *ctx = user_data;

    return ctx->chunk_cb(buf, buf_size, chunk_len, is_last, ctx->user_data);
}

static int coap_forward_pouch(pouch_gateway_cloud_upload_chunk_cb_t chunk_cb,
                              void *chunk_arg,
                              pouch_gateway_cloud_block2_cb_t resp_cb,
                              void *resp_arg)
{
    int err;

    k_mutex_lock(&pouch_coap_mutex, K_FOREVER);

    err = pouch_coap_setup_socket();
    if (err)
    {
        goto cleanup;
    }

    err = pouch_coap_fetch_server_cert();
    if (err)
    {
        goto cleanup;
    }

    err = pouch_coap_upload_cert();
    if (err)
    {
        goto cleanup;
    }

    publish_server_cert_to_gateway();

    struct forward_pouch_ctx resp_ctx = {
        .resp_cb = resp_cb,
        .user_data = resp_arg,
    };

    struct forward_pouch_chunk_ctx chunk_ctx = {
        .chunk_cb = chunk_cb,
        .user_data = chunk_arg,
    };

    err = pouch_coap_blockwise_post_streaming(COAP_PATH_POUCH,
                                              forward_pouch_chunk_wrapper,
                                              &chunk_ctx,
                                              resp_cb ? forward_pouch_block2_wrapper : NULL,
                                              &resp_ctx);
    if (err)
    {
        LOG_ERR("Failed to forward pouch: %d", err);
        goto cleanup;
    }

    LOG_INF("Gateway pouch forwarded");
    k_mutex_unlock(&pouch_coap_mutex);
    return 0;

cleanup:
    pouch_coap_close_connection();
    k_mutex_unlock(&pouch_coap_mutex);
    return err;
}

static int coap_upload_device_cert(const uint8_t *cert, size_t len)
{
    int err;

    k_mutex_lock(&pouch_coap_mutex, K_FOREVER);

    err = pouch_coap_setup_socket();
    if (err)
    {
        goto cleanup;
    }

    err = pouch_coap_fetch_server_cert();
    if (err)
    {
        goto cleanup;
    }

    publish_server_cert_to_gateway();

    err = pouch_coap_blockwise_post(COAP_PATH_DEVICE_CERT, cert, len, NULL, NULL);
    if (err)
    {
        LOG_ERR("Failed to upload peripheral device cert: %d", err);
        goto cleanup;
    }

    LOG_INF("Peripheral device certificate uploaded (%zu bytes)", len);
    k_mutex_unlock(&pouch_coap_mutex);
    return 0;

cleanup:
    pouch_coap_close_connection();
    k_mutex_unlock(&pouch_coap_mutex);
    return err;
}

/*--------------------------------------------------
 * Registration
 *------------------------------------------------*/

static const struct pouch_gateway_cloud_transport coap_cloud_transport = {
    .ensure_ready = coap_ensure_ready,
    .forward_pouch = coap_forward_pouch,
    .upload_device_cert = coap_upload_device_cert,
};

void pouch_coap_gateway_init(void)
{
    pouch_gateway_cloud_transport_register(&coap_cloud_transport);
}
