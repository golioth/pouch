/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/coap.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/sys/atomic.h>

#include <pouch/transport/coap/client.h>
#include <pouch/transport/certificate.h>
#include <pouch/transport/downlink.h>
#include <pouch/transport/uplink.h>

#include "coap_internal.h"

LOG_MODULE_REGISTER(pouch_coap, CONFIG_POUCH_COAP_CLIENT_LOG_LEVEL);

#define COAP_PATH_SERVER_CERT "/.g/server-cert"
#define COAP_PATH_DEVICE_CERT "/.g/device-cert"
#define COAP_PATH_POUCH "/.g/pouch"

#define COAP_MAX_RETRIES 3
#define COAP_RETRY_TIMEOUT_MS (CONFIG_POUCH_COAP_REQUEST_TIMEOUT_S * 1000)
#define POUCH_FILL_COUNT_LIMIT 100

/*
 * Buffer size for CoAP packets.
 * Header (~48 bytes) + block payload (up to POUCH_COAP_BLOCK_SIZE).
 */
#define COAP_HEADER_OVERHEAD 48
#define COAP_BUF_SIZE (COAP_HEADER_OVERHEAD + CONFIG_POUCH_COAP_BLOCK_SIZE)

static atomic_t cert_flow_flags;
#define SERVER_CERT_DOWNLOADED_BIT BIT(0)
#define POUCH_CERT_UPLOADED_BIT BIT(1)

/*
 * All CoAP state below is accessed from a single thread only.
 * The caller must ensure that pouch_coap_client_sync() is not
 * called concurrently.
 */
static int coap_sock = -1;
static sec_tag_t sec_tag_val;

static uint8_t coap_send_buf[COAP_BUF_SIZE];
static uint8_t coap_recv_buf[COAP_BUF_SIZE];

static uint8_t server_cert_buf[CONFIG_POUCH_SERVER_CERT_MAX_LEN];
static uint8_t pouch_uplink_payload[CONFIG_POUCH_COAP_UPLINK_MAX_SIZE];

/*--------------------------------------------------
 * CoAP helpers
 *------------------------------------------------*/

/* Send raw CoAP packet data on the socket. */
static int pouch_coap_send(const uint8_t *data, size_t len)
{
    int ret = zsock_send(coap_sock, data, len, 0);

    if (ret < 0)
    {
        return -errno;
    }

    return 0;
}

/* Send an empty ACK for a CON response. */
static int pouch_coap_send_ack(uint16_t id)
{
    struct coap_packet ack;
    uint8_t buf[4 + COAP_TOKEN_MAX_LEN];
    int ret;

    ret = coap_packet_init(&ack, buf, sizeof(buf), COAP_VERSION_1, COAP_TYPE_ACK, 0, NULL, 0, id);
    if (ret < 0)
    {
        return ret;
    }

    return pouch_coap_send(ack.data, ack.offset);
}

/*
 * Receive a CoAP response, polling with timeout.
 *
 * Parses the response into @p resp. If the response is CON,
 * an ACK is sent automatically.
 */
static int pouch_coap_recv_response(struct coap_packet *resp, int timeout_ms)
{
    struct zsock_pollfd fds = {
        .fd = coap_sock,
        .events = ZSOCK_POLLIN,
    };
    int ret;
    int rcvd;

    ret = zsock_poll(&fds, 1, timeout_ms);
    if (ret == 0)
    {
        return -ETIMEDOUT;
    }

    if (ret < 0)
    {
        return -errno;
    }

    rcvd = zsock_recv(coap_sock, coap_recv_buf, sizeof(coap_recv_buf), MSG_DONTWAIT);
    if (rcvd <= 0)
    {
        return rcvd == 0 ? -ECONNRESET : -errno;
    }

    ret = coap_packet_parse(resp, coap_recv_buf, rcvd, NULL, 0);
    if (ret < 0)
    {
        LOG_ERR("Failed to parse CoAP response: %d", ret);
        return ret;
    }

    /* Auto-ACK confirmable responses */
    if (coap_header_get_type(resp) == COAP_TYPE_CON)
    {
        int ack_ret = pouch_coap_send_ack(coap_header_get_id(resp));

        if (ack_ret < 0)
        {
            LOG_WRN("Failed to send ACK: %d", ack_ret);
        }
    }

    return 0;
}

/*
 * Send a CON request and wait for a matching response.
 *
 * Handles retransmission on timeout (up to COAP_MAX_RETRIES),
 * ignores responses with mismatched tokens, and waits for the
 * real response when the server replies with an empty ACK
 * (separate response pattern).
 */
int pouch_coap_send_and_recv(struct coap_packet *req,
                             struct coap_packet *resp,
                             const uint8_t *token,
                             uint8_t token_len)
{
    int timeout_ms = COAP_RETRY_TIMEOUT_MS;

    for (int retry = 0; retry <= COAP_MAX_RETRIES; retry++)
    {
        int ret = pouch_coap_send(req->data, req->offset);

        if (ret < 0)
        {
            LOG_ERR("Failed to send CoAP request: %d", ret);
            return ret;
        }

        ret = pouch_coap_recv_response(resp, timeout_ms);
        if (ret == -ETIMEDOUT)
        {
            LOG_WRN("CoAP request timeout (retry %d/%d)", retry, COAP_MAX_RETRIES);
            continue;
        }

        if (ret < 0)
        {
            return ret;
        }

        /* Empty ACK means a separate response follows */
        if (coap_header_get_type(resp) == COAP_TYPE_ACK && coap_header_get_code(resp) == 0)
        {
            ret = pouch_coap_recv_response(resp, timeout_ms);
            if (ret < 0)
            {
                return ret;
            }
        }

        /* Verify token matches */
        uint8_t resp_token[COAP_TOKEN_MAX_LEN];
        uint8_t resp_token_len = coap_header_get_token(resp, resp_token);

        if (resp_token_len != token_len || memcmp(resp_token, token, token_len) != 0)
        {
            LOG_WRN("Token mismatch in response, ignoring");
            continue;
        }

        return 0;
    }

    LOG_ERR("CoAP request failed after %d retries", COAP_MAX_RETRIES);
    return -ETIMEDOUT;
}

/*
 * Build a CoAP request packet.
 *
 * Initializes the packet with the given method, token, and message
 * ID, then appends the URI path and optional content format. The
 * caller appends block options and payload after this call.
 */
int pouch_coap_build_request(struct coap_packet *pkt,
                             uint8_t method,
                             const uint8_t *token,
                             uint8_t token_len,
                             uint16_t msg_id,
                             const char *path,
                             int content_format)
{
    int ret;

    ret = coap_packet_init(pkt,
                           coap_send_buf,
                           sizeof(coap_send_buf),
                           COAP_VERSION_1,
                           COAP_TYPE_CON,
                           token_len,
                           token,
                           method,
                           msg_id);
    if (ret < 0)
    {
        return ret;
    }

    ret = coap_packet_set_path(pkt, path);
    if (ret < 0)
    {
        return ret;
    }

    if (content_format >= 0)
    {
        ret = coap_append_option_int(pkt, COAP_OPTION_CONTENT_FORMAT, content_format);
        if (ret < 0)
        {
            return ret;
        }
    }

    return 0;
}

/*--------------------------------------------------
 * DTLS socket setup
 *------------------------------------------------*/

static int pouch_coap_set_sockopt_dtls(int sock, sec_tag_t sec_tag)
{
    int verify = TLS_PEER_VERIFY_REQUIRED;
    const sec_tag_t sec_tags[] = {sec_tag};
    int ret;

    ret = zsock_setsockopt(sock, SOL_TLS, TLS_PEER_VERIFY, &verify, sizeof(verify));
    if (ret < 0)
    {
        return -errno;
    }

    ret = zsock_setsockopt(sock, SOL_TLS, TLS_SEC_TAG_LIST, sec_tags, sizeof(sec_tags));
    if (ret < 0)
    {
        return -errno;
    }

    ret = zsock_setsockopt(sock,
                           SOL_TLS,
                           TLS_HOSTNAME,
                           CONFIG_POUCH_COAP_GW_URI,
                           sizeof(CONFIG_POUCH_COAP_GW_URI));
    if (ret < 0)
    {
        return -errno;
    }

    return 0;
}

static int pouch_coap_setup_socket(sec_tag_t sec_tag)
{
    struct zsock_addrinfo hints = {0};
    struct zsock_addrinfo *addrs = NULL;
    int err = 0;

    if (coap_sock >= 0)
    {
        return 0;
    }

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    LOG_DBG("Connecting to CoAP gateway: %s:%s",
            CONFIG_POUCH_COAP_GW_URI,
            CONFIG_POUCH_COAP_GW_PORT);

    err = zsock_getaddrinfo(CONFIG_POUCH_COAP_GW_URI, CONFIG_POUCH_COAP_GW_PORT, &hints, &addrs);
    if (err)
    {
        LOG_ERR("Failed to resolve address (%s:%s): %d",
                CONFIG_POUCH_COAP_GW_URI,
                CONFIG_POUCH_COAP_GW_PORT,
                err);
        return -EADDRNOTAVAIL;
    }

    coap_sock = zsock_socket(addrs->ai_family, addrs->ai_socktype, IPPROTO_DTLS_1_2);
    if (coap_sock < 0)
    {
        err = -errno;
        LOG_ERR("Failed to create DTLS socket: %d", err);
        goto cleanup;
    }

    err = pouch_coap_set_sockopt_dtls(coap_sock, sec_tag);
    if (err)
    {
        LOG_ERR("Failed setting DTLS socket options: %d", err);
        goto cleanup;
    }

    if (zsock_connect(coap_sock, addrs->ai_addr, addrs->ai_addrlen) < 0)
    {
        err = -errno;
        LOG_ERR("Failed to connect DTLS socket: %d", err);
        goto cleanup;
    }

cleanup:
    zsock_freeaddrinfo(addrs);

    if (err && coap_sock >= 0)
    {
        zsock_close(coap_sock);
        coap_sock = -1;
    }

    return err;
}

/* Close the DTLS connection and reset session state. */
static void pouch_coap_close_connection(void)
{
    if (coap_sock >= 0)
    {
        zsock_close(coap_sock);
        coap_sock = -1;
    }

    atomic_clear_bit(&cert_flow_flags, POUCH_CERT_UPLOADED_BIT);
    atomic_clear_bit(&cert_flow_flags, SERVER_CERT_DOWNLOADED_BIT);
}

/*--------------------------------------------------
 * Application-level operations
 *------------------------------------------------*/

static int pouch_coap_fetch_server_cert(void)
{
    size_t cert_len = 0;
    int err;

    if (atomic_test_bit(&cert_flow_flags, SERVER_CERT_DOWNLOADED_BIT))
    {
        return 0;
    }

    err = pouch_coap_blockwise_get(COAP_PATH_SERVER_CERT,
                                   server_cert_buf,
                                   sizeof(server_cert_buf),
                                   &cert_len);
    if (err)
    {
        LOG_ERR("Failed to fetch server certificate: %d", err);
        return err;
    }

    if (cert_len == 0)
    {
        return -ENODATA;
    }

    struct pouch_cert cert = {
        .buffer = server_cert_buf,
        .size = cert_len,
    };

    err = pouch_server_certificate_set(&cert);
    if (err)
    {
        LOG_ERR("Failed to store server certificate: %d", err);
        return err;
    }

    atomic_set_bit(&cert_flow_flags, SERVER_CERT_DOWNLOADED_BIT);
    LOG_INF("Server certificate fetch complete (%zu bytes)", cert_len);
    return 0;
}

static int pouch_coap_upload_cert(void)
{
    struct pouch_cert device_cert;
    int err;

    if (atomic_test_bit(&cert_flow_flags, POUCH_CERT_UPLOADED_BIT))
    {
        return 0;
    }

    err = pouch_device_certificate_get(&device_cert);
    if (err)
    {
        LOG_ERR("Unable to read Pouch device certificate: %d", err);
        return err;
    }

    err = pouch_coap_blockwise_post(COAP_PATH_DEVICE_CERT,
                                    device_cert.buffer,
                                    device_cert.size,
                                    NULL,
                                    NULL);
    if (err)
    {
        LOG_ERR("Failed to upload device certificate: %d", err);
        return err;
    }

    atomic_set_bit(&cert_flow_flags, POUCH_CERT_UPLOADED_BIT);
    LOG_INF("Pouch certificate upload complete");
    return 0;
}

/*--------------------------------------------------
 * Pouch sync: POST uplink, receive downlink
 *------------------------------------------------*/

struct pouch_sync_state
{
    bool downlink_started;
    size_t downlink_len;
};

static int pouch_coap_sync_block2_cb(const uint8_t *data,
                                     size_t len,
                                     bool first_block,
                                     bool last_block,
                                     void *user_data)
{
    struct pouch_sync_state *state = user_data;

    if (first_block)
    {
        pouch_downlink_start();
        state->downlink_started = true;
    }

    if (len > 0)
    {
        int err = pouch_downlink_push(data, len);

        if (err)
        {
            LOG_ERR("Failed to push downlink payload: %d", err);
            return err;
        }

        state->downlink_len += len;
    }

    if (last_block && state->downlink_started)
    {
        pouch_downlink_finish();
        state->downlink_started = false;
    }

    return 0;
}

/*
 * Fill a buffer with data from the pouch uplink, polling until
 * the buffer is full or no more data is available.
 */
static int pouch_coap_fill_uplink_buffer(struct pouch_uplink *uplink,
                                         uint8_t *buf,
                                         size_t *buf_len,
                                         bool *is_last)
{
    int fill_count = 0;
    size_t available = *buf_len;

    while (available)
    {
        size_t written = *buf_len - available;
        size_t size = available;
        enum pouch_result res;

        res = pouch_uplink_fill(uplink, buf + written, &size);

        LOG_DBG("uplink_fill res = %d, size = %zu", res, size);

        if (res == POUCH_ERROR)
        {
            return pouch_uplink_error(uplink);
        }

        available -= size;

        if (res == POUCH_NO_MORE_DATA)
        {
            *is_last = true;
            break;
        }

        if (available)
        {
            k_sleep(K_MSEC(100));
        }

        if (++fill_count > POUCH_FILL_COUNT_LIMIT)
        {
            LOG_ERR("Failed to get uplink data after %d attempts", POUCH_FILL_COUNT_LIMIT);
            return -ENOENT;
        }
    }

    *buf_len -= available;
    return 0;
}

static int pouch_coap_collect_uplink_payload(size_t *payload_len)
{
    struct pouch_uplink *uplink = pouch_uplink_start();
    size_t total_len = 0;
    bool is_last = false;
    int err = 0;

    if (uplink == NULL)
    {
        LOG_ERR("Failed to start uplink");
        return -ENOMEM;
    }

    while (!is_last)
    {
        size_t chunk_len;

        if (total_len >= sizeof(pouch_uplink_payload))
        {
            LOG_ERR(
                "Uplink payload exceeds configured max "
                "(%zu bytes)",
                sizeof(pouch_uplink_payload));
            err = -ENOMEM;
            break;
        }

        chunk_len = sizeof(pouch_uplink_payload) - total_len;

        err = pouch_coap_fill_uplink_buffer(uplink,
                                            &pouch_uplink_payload[total_len],
                                            &chunk_len,
                                            &is_last);
        if (err)
        {
            break;
        }

        total_len += chunk_len;
    }

    pouch_uplink_finish(uplink);

    if (err)
    {
        return err;
    }

    *payload_len = total_len;
    return 0;
}

static int pouch_coap_send_uplink(void)
{
    struct pouch_sync_state state = {0};
    size_t payload_len = 0;
    int err;

    err = pouch_coap_collect_uplink_payload(&payload_len);
    if (err)
    {
        LOG_ERR("Failed to collect Pouch uplink payload: %d", err);
        return err;
    }

    err = pouch_coap_blockwise_post(COAP_PATH_POUCH,
                                    pouch_uplink_payload,
                                    payload_len,
                                    pouch_coap_sync_block2_cb,
                                    &state);
    if (err)
    {
        if (state.downlink_started)
        {
            pouch_downlink_finish();
        }

        return err;
    }

    LOG_INF("Pouch uplink sync complete (tx=%zu bytes, rx=%zu bytes)",
            payload_len,
            state.downlink_len);
    return 0;
}

/*--------------------------------------------------
 * Public API
 *------------------------------------------------*/

int pouch_coap_client_init(sec_tag_t sec_tag)
{
    sec_tag_val = sec_tag;
    return 0;
}

int pouch_coap_client_sync(void)
{
    int err;

    err = pouch_coap_setup_socket(sec_tag_val);
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

    err = pouch_coap_send_uplink();
    if (err)
    {
        goto cleanup;
    }

    return 0;

cleanup:
    pouch_coap_close_connection();
    return err;
}
