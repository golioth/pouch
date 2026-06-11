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

/*
 * Buffer size for CoAP packets.
 * Header (~48 bytes) + block payload (up to POUCH_COAP_BLOCK_SIZE).
 */
#define COAP_HEADER_OVERHEAD 48
#define COAP_BUF_SIZE (COAP_HEADER_OVERHEAD + CONFIG_POUCH_COAP_BLOCK_SIZE)

static atomic_t cert_flow_flags;
#define SERVER_CERT_DOWNLOADED_BIT (0)
#define POUCH_CERT_UPLOADED_BIT (1)

/*
 * Mutex serialising all CoAP socket and buffer access.  The gateway
 * impl (gateway.c) may call into the connection helpers from BLE
 * callback threads while the device-side sync loop runs from the
 * main thread, so every public entry point locks this mutex.
 */
K_MUTEX_DEFINE(pouch_coap_mutex);

/*
 * All CoAP state below is protected by pouch_coap_mutex.
 */
static int coap_sock = -1;
static sec_tag_t sec_tag_val;

static uint8_t coap_send_buf[COAP_BUF_SIZE];
static uint8_t coap_recv_buf[COAP_BUF_SIZE];

static uint8_t server_cert_buf[CONFIG_POUCH_SERVER_CERT_MAX_LEN];

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
        LOG_WRN("Failed to parse CoAP response: %d", ret);
        return -EBADMSG;
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

static bool pouch_coap_token_matches(const struct coap_packet *resp,
                                     const uint8_t *token,
                                     uint8_t token_len)
{
    uint8_t resp_token[COAP_TOKEN_MAX_LEN];
    uint8_t resp_token_len = coap_header_get_token(resp, resp_token);

    return resp_token_len == token_len && memcmp(resp_token, token, token_len) == 0;
}

/*
 * Send a CON request and wait for the matching response.
 *
 * On timeout, retransmits the request up to COAP_MAX_RETRIES times.
 * Within each retry the function keeps draining the socket until the
 * matching response arrives or the deadline expires, so unrelated or
 * stale datagrams do not trigger spurious retransmissions.
 */
int pouch_coap_send_and_recv(struct coap_packet *req,
                             struct coap_packet *resp,
                             const uint8_t *token,
                             uint8_t token_len)
{
    uint16_t req_mid = coap_header_get_id(req);

    for (int retry = 0; retry <= COAP_MAX_RETRIES; retry++)
    {
        int64_t deadline = k_uptime_get() + COAP_RETRY_TIMEOUT_MS;
        bool empty_ack_seen = false;
        int ret;

        ret = pouch_coap_send(req->data, req->offset);
        if (ret < 0)
        {
            LOG_ERR("Failed to send CoAP request: %d", ret);
            return ret;
        }

        while (true)
        {
            int64_t now = k_uptime_get();
            int wait_ms = (now < deadline) ? (int) (deadline - now) : 0;
            uint8_t resp_type;
            uint8_t resp_code;
            uint16_t resp_mid;

            ret = pouch_coap_recv_response(resp, wait_ms);
            if (ret == -ETIMEDOUT)
            {
                break;
            }

            if (ret == -EBADMSG)
            {
                /* Malformed/unparseable datagram: ignore and keep
                 * draining until the deadline. RFC 7252 §4.2 — stray
                 * datagrams must not trigger spurious retransmissions.
                 */
                continue;
            }

            if (ret < 0)
            {
                return ret;
            }

            resp_type = coap_header_get_type(resp);
            resp_code = coap_header_get_code(resp);
            resp_mid = coap_header_get_id(resp);

            if (resp_type == COAP_TYPE_RESET)
            {
                if (resp_mid == req_mid)
                {
                    LOG_ERR("Server sent RST for request (mid=%u)", req_mid);
                    return -ECONNRESET;
                }
                continue;
            }

            if (resp_type == COAP_TYPE_ACK && resp_code == 0)
            {
                if (resp_mid != req_mid)
                {
                    LOG_DBG("Ignoring empty ACK for mid=%u (expected %u)", resp_mid, req_mid);
                    continue;
                }
                empty_ack_seen = true;
                continue;
            }

            if (resp_type == COAP_TYPE_ACK && resp_mid != req_mid)
            {
                LOG_WRN("Ignoring piggyback ACK with mid=%u (expected %u)", resp_mid, req_mid);
                continue;
            }

            if (!pouch_coap_token_matches(resp, token, token_len))
            {
                LOG_WRN("Ignoring response with mismatched token");
                continue;
            }

            return 0;
        }

        if (empty_ack_seen)
        {
            /* RFC 7252 §4.2 — once the request has been confirmed by
             * an empty ACK, the client must not retransmit the CON.
             * Give up waiting for the separate response instead.
             */
            LOG_ERR("Empty ACK received but no separate response within deadline");
            return -ETIMEDOUT;
        }

        LOG_WRN("CoAP request timeout (retry %d/%d)", retry, COAP_MAX_RETRIES);
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

    return 0;
}

/*
 * Stream a Block1 chunk from the pouch uplink into @p buf.
 *
 * Fills exactly @p buf_size bytes for every non-final chunk so that
 * each intermediate Block1 fragment matches the negotiated block
 * size, blocking on the uplink queue as needed.
 */
static int pouch_uplink_chunk_cb(uint8_t *buf,
                                 size_t buf_size,
                                 size_t *chunk_len,
                                 bool *is_last,
                                 void *user_data)
{
    struct pouch_uplink *uplink = user_data;
    size_t total = 0;
    int err;

    while (total < buf_size)
    {
        size_t requested = buf_size - total;
        enum pouch_result res = pouch_uplink_fill(uplink, buf + total, &requested);

        if (res == POUCH_ERROR)
        {
            return pouch_uplink_error(uplink);
        }

        total += requested;

        if (res == POUCH_NO_MORE_DATA)
        {
            *chunk_len = total;
            *is_last = true;
            return 0;
        }

        if (requested == 0)
        {
            err = pouch_wait_for_queue(uplink, POUCH_MSEC_INTERNAL(100));
            if (err)
            {
                LOG_ERR("Failed to wait for uplink queue: %d", err);
                return err;
            }
        }
    }

    *chunk_len = total;
    *is_last = false;
    return 0;
}

static int pouch_coap_send_uplink(void)
{
    struct pouch_sync_state state = {0};
    struct pouch_uplink *uplink;
    int err;

    uplink = pouch_uplink_start();
    if (uplink == NULL)
    {
        LOG_ERR("Failed to start uplink");
        return -ENOMEM;
    }

    err = pouch_coap_blockwise_post_streaming(COAP_PATH_POUCH,
                                              pouch_uplink_chunk_cb,
                                              uplink,
                                              pouch_coap_sync_block2_cb,
                                              &state);

    pouch_uplink_finish(uplink);
    pouch_downlink_finish();

    if (err)
    {
        LOG_ERR("Pouch CoAP sync failed: %d", err);
        return err;
    }

    LOG_INF("Pouch sync complete (rx=%zu bytes)", state.downlink_len);
    return 0;
}

/*--------------------------------------------------
 * Public API
 *------------------------------------------------*/

int pouch_coap_client_init(sec_tag_t sec_tag)
{
    k_mutex_lock(&pouch_coap_mutex, K_FOREVER);
    pouch_coap_close_connection();
    sec_tag_val = sec_tag;
    k_mutex_unlock(&pouch_coap_mutex);
    return 0;
}

int pouch_coap_client_sync(void)
{
    int err;

    if (sec_tag_val <= 0)
    {
        LOG_ERR("sec_tag not set");
        return -ENOENT;
    }

    k_mutex_lock(&pouch_coap_mutex, K_FOREVER);

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

    k_mutex_unlock(&pouch_coap_mutex);
    return 0;

cleanup:
    pouch_coap_close_connection();
    k_mutex_unlock(&pouch_coap_mutex);
    return err;
}
