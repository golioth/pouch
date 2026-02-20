/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <sys/errno.h>
#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/http/client.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/sys/atomic.h>

#include <pouch/transport/http/client.h>
#include <pouch/transport/certificate.h>
#include <pouch/transport/downlink.h>
#include <pouch/transport/uplink.h>

#include <mbedtls/ssl_ciphersuites.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(pouch_http);

#define HTTP_TIMEOUT_MS CONFIG_POUCH_HTTP_TIMEOUT_S *MSEC_PER_SEC
#define HTTP_PATH_POUCH "/.g/pouch"
#define HTTP_PATH_SERVER_CERT "/.g/server-cert"
#define HTTP_PATH_DEVICE_CERT "/.g/device-cert"

#define POUCH_FILL_COUNT_LIMIT 100
#define CHUNK_DATA_LEN_FMT "%04x"
#define CHUNK_HEADER_LEN 6  // ascii hex len(4) + CRLF(2)
#define CHUNK_FOOTER_LEN 2  // CRLF(2)

static atomic_t pouch_cert_flags;
#define SERVER_CERT_DOWNLOADED_BIT BIT(0)
#define POUCH_CERT_UPLOADED_BIT BIT(1)

static int _sock;

#define IN_USE_FLAG BIT(0)

static struct sync_context
{
    atomic_t flags;
    struct http_request *req;
    struct pouch_uplink *uplink;

    uint32_t rcv_offset;

    uint8_t recv_buf[CONFIG_POUCH_HTTP_RCV_BUF_SIZE];
    uint8_t scratch[CONFIG_POUCH_HTTP_SCRATCH_BUF_SIZE];
} _sync_ctx;

static struct get_server_cert_context
{
    uint8_t cert_buf[CONFIG_POUCH_HTTP_SERVER_CRT_MAX_SIZE];
    size_t pos;
} _server_cert_ctx;

static int pouch_server_cert_response_callback(struct http_response *rsp,
                                               enum http_final_call final_data,
                                               void *user_data)
{
    LOG_DBG("Received http cert response %d bytes with code %i",
            rsp->data_len,
            rsp->http_status_code);

    int err = 0;

    if (false == rsp->body_found)
    {
        LOG_ERR("Failed to find HTTP body");
        return -EINVAL;
    }

    struct get_server_cert_context *ctx = user_data;

    if (ctx->pos + rsp->body_frag_len > sizeof(ctx->cert_buf))
    {
        LOG_ERR("Server cert too large for buffer");
        return -ENOMEM;
    }

    memcpy(ctx->cert_buf + ctx->pos, rsp->body_frag_start, rsp->body_frag_len);
    ctx->pos += rsp->body_frag_len;

    if (HTTP_DATA_FINAL == final_data)
    {
        struct pouch_cert cert = {
            .buffer = ctx->cert_buf,
            .size = ctx->pos,
        };
        pouch_server_certificate_set(&cert);
        atomic_set_bit(&pouch_cert_flags, SERVER_CERT_DOWNLOADED_BIT);
    }

    return err;
}

static int pouch_http_device_cert_callback(struct http_response *rsp,
                                           enum http_final_call final_data,
                                           void *user_data)
{
    int err = 0;

    if (!IN_RANGE(rsp->http_status_code, 200, 299))
    {
        LOG_ERR("Failed to upload Pouch cert: %u", rsp->http_status_code);
        err = -EIO;
    }
    else
    {
        atomic_set_bit(&pouch_cert_flags, POUCH_CERT_UPLOADED_BIT);
    }

    return err;
}

static int pouch_http_response_callback(struct http_response *rsp,
                                        enum http_final_call final_data,
                                        void *user_data)
{
    LOG_DBG("Received http response %d bytes with code %i", rsp->data_len, rsp->http_status_code);
    struct sync_context *ctx = user_data;

    if (!IN_RANGE(rsp->http_status_code, 200, 299))
    {
        LOG_ERR("HTTP request failed: %u", rsp->http_status_code);
        pouch_uplink_finish(ctx->uplink);
        return -EINVAL;
    }

    if (0 == ctx->rcv_offset)
    {
        pouch_uplink_finish(ctx->uplink);
        pouch_downlink_start();
    }

    pouch_downlink_push(rsp->body_frag_start, rsp->body_frag_len);
    ctx->rcv_offset += rsp->body_frag_len;

    if (HTTP_DATA_FINAL == final_data)
    {
        LOG_INF("Received final downlink block");
        pouch_downlink_finish();
    }

    return 0;
}

/**
 * Fill a buffer with data from a given Pouch uplink
 *
 * @param uplink The uplink struct that will provide the data
 * @param buf Buffer into which data should be written.
 * @param[in,out] buf_len As an input, the length of /buf, as an output, the length of data written
 *                        to /buf
 * @param[out] is_last Set to true when no more data is available
 */
static int pouch_fill_data_cb(struct pouch_uplink *uplink,
                              uint8_t *buf,
                              size_t *buf_len,
                              bool *is_last)
{
    int8_t fill_count = 0;
    size_t available_space = *buf_len;
    while (available_space)
    {
        size_t size = available_space;
        size_t used_space = *buf_len - available_space;
        enum pouch_result res = pouch_uplink_fill(uplink, buf + used_space, &size);
        LOG_DBG("uplink_fill res = %d, size = %d", res, size);
        if (POUCH_ERROR == res)
        {
            return pouch_uplink_error(uplink);
        }

        available_space -= size;

        if (POUCH_NO_MORE_DATA == res)
        {
            *is_last = true;
            break;
        }

        if (available_space)
        {
            k_sleep(K_MSEC(100));
        }

        if (POUCH_FILL_COUNT_LIMIT < ++fill_count)
        {
            LOG_ERR("Failed to get uplink data after %d attempts", POUCH_FILL_COUNT_LIMIT);
            return -ENOENT;
        }
    }

    *buf_len = *buf_len - available_space;
    return 0;
}

/**
 * Write the header and foot for chunked transfer encoding
 *
 * This helper function writes the data length and CRLF to the header as well as the CRLF to the
 * footer.
 *
 * @param buf Buffer containing the data payler which must be offset by CHUNK_HEADER_LEN
 * @param data_len Length of the data being sent in this chunk. This value includes only the length
 *                 of the data itself and doesn't account for any offset, header, or footer.
 */
static int write_chunk_header_footer(uint8_t *buf, size_t data_len)
{
    snprintk(buf, CHUNK_HEADER_LEN - 1, CHUNK_DATA_LEN_FMT, data_len);
    buf[CHUNK_HEADER_LEN - 2] = '\r';
    buf[CHUNK_HEADER_LEN - 1] = '\n';
    buf[CHUNK_HEADER_LEN + data_len] = '\r';
    buf[CHUNK_HEADER_LEN + data_len + 1] = '\n';

    return CHUNK_HEADER_LEN + data_len + CHUNK_FOOTER_LEN;
}

/**
 * Callback for the http_client to supply uplink data
 *
 * The Pouch uplink size is unknown when the HTTP request starts. This callback will be run once by
 * the http_client and should call zsock_send() as many times as necessary to transfer all
 * chunk-encoded data.
 */
static int pouch_http_payload_callback(int sock, struct http_request *req, void *user_data)
{
    struct sync_context *ctx = user_data;

    LOG_INF("HTTP Client requesting payload");

    int err = 0;
    size_t tx_len = 0;
    size_t pouch_data_len = 0;
    size_t payload_size = 0;
    bool last_chunk = false;

    do
    {
        pouch_data_len = sizeof(ctx->scratch) - CHUNK_HEADER_LEN - CHUNK_FOOTER_LEN;

        err = pouch_fill_data_cb(ctx->uplink,
                                 ctx->scratch + CHUNK_HEADER_LEN,
                                 &pouch_data_len,
                                 &last_chunk);
        if (0 != err)
        {
            LOG_ERR("Error getting pouch data: %d", err);
            pouch_uplink_finish(ctx->uplink);
            return -EINVAL;
        }

        tx_len = write_chunk_header_footer(ctx->scratch, pouch_data_len);
        payload_size += zsock_send(sock, ctx->scratch, tx_len, 0);

    } while (false == last_chunk);

    /* Write final zero chunk */
    ctx->scratch[CHUNK_HEADER_LEN] = 0;
    tx_len = write_chunk_header_footer(ctx->scratch, 0);
    payload_size += zsock_send(sock, ctx->scratch, tx_len, 0);

    LOG_INF("Uplink payload sent: %zu bytes", payload_size);
    return payload_size;
}

static int pouch_http_set_sockopt_tls(int sock, sec_tag_t sec_tag)
{
    int ret = zsock_setsockopt(sock, SOL_TLS, TLS_SEC_TAG_LIST, &sec_tag, sizeof(sec_tag_t));
    if (ret < 0)
    {
        return -errno;
    }

    ret = zsock_setsockopt(sock,
                           SOL_TLS,
                           TLS_HOSTNAME,
                           CONFIG_POUCH_HTTP_GW_URI,
                           sizeof(CONFIG_POUCH_HTTP_GW_URI));
    if (ret < 0)
    {
        return -errno;
    }

    return 0;
}

int pouch_http_client_init(sec_tag_t sec_tag)
{
    static struct zsock_addrinfo hints;
    struct zsock_addrinfo *addrs;
    int ret;
    int err = 0;
    _sock = 0;

    atomic_clear_bit(&_sync_ctx.flags, IN_USE_FLAG);

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    ret = zsock_getaddrinfo(CONFIG_POUCH_HTTP_GW_URI, CONFIG_POUCH_HTTP_GW_PORT, &hints, &addrs);

    if (ret < 0)
    {
        LOG_ERR("Failed to resolve address (%s:%s) %d",
                CONFIG_POUCH_HTTP_GW_URI,
                CONFIG_POUCH_HTTP_GW_PORT,
                ret);
        return ret;
    }

    _sock = zsock_socket(addrs->ai_family, addrs->ai_socktype, IPPROTO_TLS_1_2);
    if (_sock < 0)
    {
        err = -errno;
        LOG_ERR("Failed to create socket: %d", err);
        goto finish;
    }

    err = pouch_http_set_sockopt_tls(_sock, sec_tag);
    if (err)
    {
        LOG_ERR("Failed to set TLS socket options: %d", err);
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

static int fetch_server_cert(struct sync_context *sync)
{
    if (true == atomic_test_bit(&pouch_cert_flags, SERVER_CERT_DOWNLOADED_BIT))
    {
        return 0;
    }

    struct get_server_cert_context *cert_ctx = &_server_cert_ctx;
    cert_ctx->pos = 0;

    struct http_request req = {
        .method = HTTP_GET,
        .url = HTTP_PATH_SERVER_CERT,
        .host = CONFIG_POUCH_HTTP_GW_URI,
        .port = CONFIG_POUCH_HTTP_GW_PORT,
        .protocol = "HTTP/1.1",
        .response = pouch_server_cert_response_callback,
        .recv_buf = sync->recv_buf,
        .recv_buf_len = sizeof(sync->recv_buf),
    };

    int ret = http_client_req(_sock, &req, HTTP_TIMEOUT_MS, cert_ctx);
    if (ret < 0)
    {
        LOG_ERR("Failed to send HTTP request for server cert");
        return ret;
    }

    if (false == atomic_test_bit(&pouch_cert_flags, SERVER_CERT_DOWNLOADED_BIT))
    {
        return -EIO;
    }

    LOG_INF("Server certificate fetch complete.");
    return 0;
}

static int upload_pouch_cert(struct sync_context *sync)
{
    if (true == atomic_test_bit(&pouch_cert_flags, POUCH_CERT_UPLOADED_BIT))
    {
        return 0;
    }

    struct pouch_cert device_cert;
    int err = pouch_device_certificate_get(&device_cert);
    if (0 != err)
    {
        LOG_ERR("Unable to read pouch device certificate: %d", err);
        return err;
    }

    sync->rcv_offset = 0;

    struct http_request req = {
        .method = HTTP_POST,
        .url = HTTP_PATH_DEVICE_CERT,
        .host = CONFIG_POUCH_HTTP_GW_URI,
        .port = CONFIG_POUCH_HTTP_GW_PORT,
        .protocol = "HTTP/1.1",
        .content_type_value = "application/octet-stream",
        .payload = device_cert.buffer,
        .payload_len = device_cert.size,
        .response = pouch_http_device_cert_callback,
        .recv_buf = sync->recv_buf,
        .recv_buf_len = sizeof(sync->recv_buf),
    };

    int ret = http_client_req(_sock, &req, HTTP_TIMEOUT_MS, sync);
    if (ret < 0)
    {
        LOG_ERR("Failed to send HTTP request: %d", errno);
        return ret;
    }

    if (false == atomic_test_bit(&pouch_cert_flags, POUCH_CERT_UPLOADED_BIT))
    {
        return -EIO;
    }

    LOG_INF("Pouch certificate upload complete.");
    return 0;
}

static int send_pouch_uplink(struct sync_context *sync)
{
    sync->uplink = pouch_uplink_start();
    if (NULL == sync->uplink)
    {
        LOG_ERR("Failed to start uplink");
        return -ENOMEM;
    }

    const char *headers[] = {"Transfer-Encoding: chunked\r\n", NULL};

    sync->rcv_offset = 0;

    struct http_request req = {
        .method = HTTP_POST,
        .url = HTTP_PATH_POUCH,
        .host = CONFIG_POUCH_HTTP_GW_URI,
        .port = CONFIG_POUCH_HTTP_GW_PORT,
        .protocol = "HTTP/1.1",
        .header_fields = headers,
        .content_type_value = "application/octet-stream",
        .payload = NULL,
        .payload_len = 0,
        .payload_cb = pouch_http_payload_callback,
        .response = pouch_http_response_callback,
        .recv_buf = sync->recv_buf,
        .recv_buf_len = sizeof(sync->recv_buf),
    };

    int err = http_client_req(_sock, &req, HTTP_TIMEOUT_MS, sync);
    if (err < 0)
    {
        LOG_ERR("Failed to send HTTP request: %d", errno);
        return err;
    }

    return 0;
}

int pouch_http_client_sync(void)
{
    struct sync_context *sync = &_sync_ctx;

    if (true == atomic_test_and_set_bit(&sync->flags, IN_USE_FLAG))
    {
        LOG_WRN("Sync already in progress, aborting.");
        return -EACCES;
    }

    int err = fetch_server_cert(sync);
    if (0 != err)
    {
        LOG_ERR("Failed to download server certificate: %d", err);
        goto clear_and_return;
    }

    err = upload_pouch_cert(sync);
    if (0 != err)
    {
        LOG_ERR("Failed to upload Pouch certificate: %d", err);
        goto clear_and_return;
    }

    err = send_pouch_uplink(sync);
    if (0 != err)
    {
        LOG_ERR("Failed to send Pouch uplink: %d", err);
        goto clear_and_return;
    }

clear_and_return:
    atomic_clear_bit(&sync->flags, IN_USE_FLAG);
    return err;
}
