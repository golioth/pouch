/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/net/coap_client.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>

#include <pouch/transport/coap/pouch_coap.h>
#include <pouch/transport/certificate.h>
#include <pouch/transport/downlink.h>
#include <pouch/transport/uplink.h>

#include <mbedtls/ssl_ciphersuites.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(pouch_coap, CONFIG_POUCH_COAP_LOG_LEVEL);

#define CONFIG_POUCH_COAP_HOST_URI "coap.golioth.io"

static struct coap_client _client;
static int _sock;

struct sync_context
{
    struct coap_client_request req;
    struct pouch_uplink *uplink;
    struct pouch_downlink *downlink;
    uint8_t scratch[1024];
};

struct get_server_cert_context
{
    struct coap_client_request req;
    uint8_t cert_buf[4096];
    size_t pos;
} _server_cert_ctx;

static void pouch_server_cert_response_callback(const struct coap_client_response_data *data, void *user_data)
{
    struct get_server_cert_context *ctx = user_data;

    if (data->packet == NULL)
    {
        LOG_ERR("Error response getting server cert");
        return;
    }

    // LOG_INF("Received cert response %d bytes, (pos %d, offset %d)", data->payload_len, ctx->pos, data->offset);

    memcpy(ctx->cert_buf + ctx->pos, data->payload, data->payload_len);
    ctx->pos += data->payload_len;

    if (data->last_block)
    {
        struct pouch_cert cert =
        {
            .buffer = ctx->cert_buf,
            .size = ctx->pos,
        };
        pouch_server_certificate_set(&cert);
    }
}

static void pouch_coap_response_callback(const struct coap_client_response_data *data, void *user_data)
{
    LOG_INF("Received coap response %d bytes with code %d", data->payload_len, data->result_code);
    struct sync_context *ctx = user_data;

    if (0 == data->offset)
    {
        pouch_uplink_finish(ctx->uplink);
        pouch_downlink_start();
    }

    pouch_downlink_push(data->payload, data->payload_len);

    if (data->last_block)
    {
        // LOG_INF("Received final downlink block");
        pouch_downlink_finish();
    }

    free(ctx);
}

static int pouch_coap_payload_callback(size_t offset, const uint8_t **payload, size_t *len, bool *last_block, void *user_data)
{
    struct sync_context *ctx = user_data;

    LOG_INF("CoAP Client requesting payload");

    size_t available_space = sizeof(ctx->scratch);
    while (available_space)
    {
        size_t size = available_space;
        size_t used_space = sizeof(ctx->scratch) - available_space;
        enum pouch_result res = pouch_uplink_fill(ctx->uplink, ctx->scratch + used_space, &size);
        LOG_INF("uplink_fill res = %d, size = %d", res, size);
        if (POUCH_ERROR == res)
        {
            int err = pouch_uplink_error(ctx->uplink);
            LOG_ERR("Received pouch error %d", err);
            pouch_uplink_finish(ctx->uplink);
            free(ctx);
            return err;
        }

        available_space -= size;

        if (POUCH_NO_MORE_DATA == res)
        {
            *last_block = true;
            break;
        }

        if (POUCH_MORE_DATA == res)
        {
            *last_block = false;
        }

        if (available_space)
        {
            k_sleep(K_MSEC(100));
        }
    }

    *len = sizeof(ctx->scratch) - available_space;
    *payload = ctx->scratch;

    LOG_INF("CoAP payload callback %d bytes, (available %d)", *len, available_space);

    return 0;
}

static int pouch_coap_set_sockopt_dtls(int sock, const sec_tag_t *sec_tag_list, size_t sec_tag_count)
{
    int ret = zsock_setsockopt(sock,
                               SOL_TLS,
                               TLS_SEC_TAG_LIST,
                               sec_tag_list,
                               sec_tag_count * sizeof(sec_tag_t));
    if (ret < 0)
    {
        return -errno;
    }

    static const int ciphersuites[] = {
        MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
    };
    ret = zsock_setsockopt(sock,
                               SOL_TLS,
                               TLS_CIPHERSUITE_LIST,
                               ciphersuites,
                               sizeof(ciphersuites));
    if (ret < 0)
    {
        return -errno;
    }

    ret = zsock_setsockopt(sock,
                           SOL_TLS,
                           TLS_HOSTNAME,
                           CONFIG_POUCH_COAP_HOST_URI, sizeof(CONFIG_POUCH_COAP_HOST_URI));
    if (ret < 0)
    {
        return -errno;
    }

    return 0;
}

int pouch_coap_init(const sec_tag_t *sec_tag_list, size_t sec_tag_count)
{
    _sock = 0;

    int err = coap_client_init(&_client, NULL);
    if (err)
    {
        LOG_ERR("Failed to initialize CoAP client: %d", err);
        goto finish;
    }

    struct zsock_addrinfo hints = {
        .ai_family = AF_UNSPEC,
        .ai_socktype = SOCK_DGRAM,
        .ai_protocol = IPPROTO_UDP,
    };
    struct zsock_addrinfo *addrs = NULL;
    int ret = zsock_getaddrinfo(CONFIG_POUCH_COAP_HOST_URI, "5684", &hints, &addrs);
    if (ret < 0)
    {
        LOG_ERR("Failed to resolve address (%s:%s) %d", CONFIG_POUCH_COAP_HOST_URI, "5684", ret);
        return ret;
    }

    _sock = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_DTLS_1_2);
    if (_sock < 0)
    {
        err = -errno;
        LOG_ERR("Failed to create socket: %d", err);
        goto finish;
    }

    err = pouch_coap_set_sockopt_dtls(_sock, sec_tag_list, sec_tag_count);
    if (err)
    {
        LOG_ERR("Failed to set DTLS socket options: %d", err);
        goto finish;
    }

    ret = zsock_connect(_sock, addrs->ai_addr, addrs->ai_addrlen);
    if (ret < 0)
    {
        err = -errno;
        LOG_ERR("Failed to connect to socket: %d", err);
    }

finish:
    if (err)
    {
        if (_sock >= 0)
        {
            zsock_close(_sock);
        }
    }
    if (addrs)
    {
        zsock_freeaddrinfo(addrs);
    }

    return err;
}

static int fetch_server_cert(void)
{
    struct get_server_cert_context *ctx = &_server_cert_ctx;

    ctx->pos = 0;

    ctx->req.method = COAP_METHOD_GET;
    ctx->req.confirmable = true;
    ctx->req.fmt = COAP_CONTENT_FORMAT_APP_OCTET_STREAM;
    strncpy(ctx->req.path, ".g/server-cert", CONFIG_COAP_CLIENT_MAX_PATH_LENGTH + 1);
    ctx->req.payload = NULL;
    ctx->req.len = 0;
    ctx->req.payload_cb = NULL;
    ctx->req.cb = pouch_server_cert_response_callback;
    ctx->req.user_data = ctx;
    int ret = coap_client_req(&_client, _sock, NULL, &ctx->req, NULL);
    if (ret < 0)
    {
        LOG_ERR("Failed to send CoAP request for server cert");
        return ret;
    }

    k_sleep(K_SECONDS(7));
}

int pouch_coap_sync(void)
{
    fetch_server_cert();

    struct sync_context *sync = malloc(sizeof(struct sync_context));
    if (NULL == sync)
    {
        return -ENOMEM;
    }

    sync->uplink = pouch_uplink_start();
    if (NULL == sync->uplink)
    {
        LOG_ERR("Failed to start uplink");
        free(sync);
        return -ENOMEM;
    }

    sync->req.method = COAP_METHOD_POST;
    sync->req.confirmable = true;
    sync->req.fmt = COAP_CONTENT_FORMAT_APP_OCTET_STREAM;
    strncpy(sync->req.path, ".g/pouch", CONFIG_COAP_CLIENT_MAX_PATH_LENGTH + 1);
    sync->req.payload = NULL;
    sync->req.len = 0;
    sync->req.payload_cb = pouch_coap_payload_callback;
    sync->req.cb = pouch_coap_response_callback;
    sync->req.user_data = sync;
    sync->req.options[0] = coap_client_option_initial_block2();
    int ret = coap_client_req(&_client, _sock, NULL, &sync->req, NULL);
    if (ret < 0)
    {
        LOG_ERR("Failed to send CoAP request");
        return ret;
    }

    return 0;
}