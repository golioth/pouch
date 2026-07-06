/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Stub endpoint implementations for the serial exchange integration test.
 *
 * Provides all 10 endpoint symbols (5 broker + 5 device) so that the test
 * links both broker.c and device.c without pulling in the real endpoint
 * implementations or their gateway/transport dependencies.
 *
 * Broker server_cert and device_cert stubs set the corresponding provisioning
 * flags on the gateway node (via bearer->ctx), matching the real endpoint
 * behaviour required by the broker's next() state machine.
 */

#include "stub_endpoints.h"
#include "transport/bearer.h"
#include "gateway/types.h"

#include <string.h>

struct broker_stubs broker_stubs;
struct device_stubs device_stubs;

static void sender_reset(struct stub_sender *s)
{
    memset(s, 0, sizeof(*s));
    s->send_err_after = -1;
}

static void receiver_reset(struct stub_receiver *r)
{
    memset(r, 0, sizeof(*r));
}

void stubs_reset(void)
{
    sender_reset(&device_stubs.info);
    receiver_reset(&device_stubs.server_cert);
    sender_reset(&device_stubs.device_cert);
    receiver_reset(&device_stubs.downlink);
    sender_reset(&device_stubs.uplink);

    receiver_reset(&broker_stubs.info);
    sender_reset(&broker_stubs.server_cert);
    receiver_reset(&broker_stubs.device_cert);
    sender_reset(&broker_stubs.downlink);
    receiver_reset(&broker_stubs.uplink);
}

void stub_sender_set_data(struct stub_sender *s, const uint8_t *data, size_t len)
{
    if (len > STUB_EP_BUF_SIZE)
    {
        len = STUB_EP_BUF_SIZE;
    }

    memcpy(s->tx_buf, data, len);
    s->tx_len = len;
    s->tx_offset = 0;
}

static enum pouch_result sender_fill(struct stub_sender *s, void *dst, size_t *dst_len)
{
    if (s->send_err_after >= 0 && s->send_count >= s->send_err_after)
    {
        s->send_count++;
        *dst_len = 0;
        return POUCH_ERROR;
    }

    s->send_count++;

    size_t remaining = s->tx_len - s->tx_offset;

    if (*dst_len > remaining)
    {
        *dst_len = remaining;
    }

    memcpy(dst, &s->tx_buf[s->tx_offset], *dst_len);
    s->tx_offset += *dst_len;

    return (s->tx_offset >= s->tx_len) ? POUCH_NO_MORE_DATA : POUCH_MORE_DATA;
}

static int receiver_push(struct stub_receiver *r, const void *buf, size_t len)
{
    if (r->recv_err)
    {
        return r->recv_err;
    }

    size_t space = STUB_EP_BUF_SIZE - r->rx_len;

    if (len > space)
    {
        len = space;
    }

    memcpy(&r->rx_buf[r->rx_len], buf, len);
    r->rx_len += len;
    return 0;
}

static int sender_start_helper(struct stub_sender *s)
{
    s->start_count++;
    if (s->start_err)
    {
        return s->start_err;
    }

    s->tx_offset = 0;
    return 0;
}

static int receiver_start_helper(struct stub_receiver *r)
{
    r->start_count++;
    if (r->start_err)
    {
        return r->start_err;
    }

    r->rx_len = 0;
    return 0;
}

/* ---- Broker endpoint stubs ----------------------------------------------- */

/* INFO: broker receives from device (receiver) */

static int b_info_start(struct pouch_bearer *bearer)
{
    (void) bearer;
    return receiver_start_helper(&broker_stubs.info);
}

static int b_info_recv(struct pouch_bearer *bearer, const void *buf, size_t len)
{
    (void) bearer;
    return receiver_push(&broker_stubs.info, buf, len);
}

static void b_info_end(struct pouch_bearer *bearer, bool success)
{
    (void) bearer;
    if (success)
    {
        broker_stubs.info.end_success_count++;
    }
    else
    {
        broker_stubs.info.end_fail_count++;
    }
}

const struct pouch_endpoint broker_endpoint_info = {
    .start = b_info_start,
    .recv = b_info_recv,
    .end = b_info_end,
};

/* SERVER_CERT: broker sends to device (sender) */

static int b_server_cert_start(struct pouch_bearer *bearer)
{
    (void) bearer;
    return sender_start_helper(&broker_stubs.server_cert);
}

static enum pouch_result b_server_cert_send(struct pouch_bearer *bearer, void *dst, size_t *dst_len)
{
    (void) bearer;
    return sender_fill(&broker_stubs.server_cert, dst, dst_len);
}

static void b_server_cert_end(struct pouch_bearer *bearer, bool success)
{
    if (success)
    {
        broker_stubs.server_cert.end_success_count++;

        /* Match real endpoint: mark server cert as provisioned so the broker's
         * next() state machine advances past the SERVER_CERT phase. */
        struct pouch_gateway_node_info *node = bearer->ctx;
        if (node != NULL)
        {
            node->server_cert_provisioned = true;
        }
    }
    else
    {
        broker_stubs.server_cert.end_fail_count++;
    }
}

const struct pouch_endpoint broker_endpoint_server_cert = {
    .start = b_server_cert_start,
    .send = b_server_cert_send,
    .end = b_server_cert_end,
};

/* DEVICE_CERT: broker receives from device (receiver) */

static int b_device_cert_start(struct pouch_bearer *bearer)
{
    (void) bearer;
    return receiver_start_helper(&broker_stubs.device_cert);
}

static int b_device_cert_recv(struct pouch_bearer *bearer, const void *buf, size_t len)
{
    (void) bearer;
    return receiver_push(&broker_stubs.device_cert, buf, len);
}

static void b_device_cert_end(struct pouch_bearer *bearer, bool success)
{
    if (success)
    {
        broker_stubs.device_cert.end_success_count++;

        /* Match real endpoint: mark device cert as provisioned. */
        struct pouch_gateway_node_info *node = bearer->ctx;
        if (node != NULL)
        {
            node->device_cert_provisioned = true;
        }
    }
    else
    {
        broker_stubs.device_cert.end_fail_count++;
    }
}

const struct pouch_endpoint broker_endpoint_device_cert = {
    .start = b_device_cert_start,
    .recv = b_device_cert_recv,
    .end = b_device_cert_end,
};

/* DOWNLINK: broker sends to device (sender) */

static int b_downlink_start(struct pouch_bearer *bearer)
{
    (void) bearer;
    return sender_start_helper(&broker_stubs.downlink);
}

static enum pouch_result b_downlink_send(struct pouch_bearer *bearer, void *dst, size_t *dst_len)
{
    (void) bearer;
    return sender_fill(&broker_stubs.downlink, dst, dst_len);
}

static void b_downlink_end(struct pouch_bearer *bearer, bool success)
{
    (void) bearer;
    if (success)
    {
        broker_stubs.downlink.end_success_count++;
    }
    else
    {
        broker_stubs.downlink.end_fail_count++;
    }
}

const struct pouch_endpoint broker_endpoint_downlink = {
    .start = b_downlink_start,
    .send = b_downlink_send,
    .end = b_downlink_end,
};

/* UPLINK: broker receives from device (receiver) */

static int b_uplink_start(struct pouch_bearer *bearer)
{
    (void) bearer;
    return receiver_start_helper(&broker_stubs.uplink);
}

static int b_uplink_recv(struct pouch_bearer *bearer, const void *buf, size_t len)
{
    (void) bearer;
    return receiver_push(&broker_stubs.uplink, buf, len);
}

static void b_uplink_end(struct pouch_bearer *bearer, bool success)
{
    (void) bearer;
    if (success)
    {
        broker_stubs.uplink.end_success_count++;
    }
    else
    {
        broker_stubs.uplink.end_fail_count++;
    }
}

const struct pouch_endpoint broker_endpoint_uplink = {
    .start = b_uplink_start,
    .recv = b_uplink_recv,
    .end = b_uplink_end,
};

/* ---- Device endpoint stubs ----------------------------------------------- */

/* INFO: device sends to broker (sender) */

static int d_info_start(struct pouch_bearer *bearer)
{
    (void) bearer;
    return sender_start_helper(&device_stubs.info);
}

static enum pouch_result d_info_send(struct pouch_bearer *bearer, void *dst, size_t *dst_len)
{
    (void) bearer;
    return sender_fill(&device_stubs.info, dst, dst_len);
}

static void d_info_end(struct pouch_bearer *bearer, bool success)
{
    (void) bearer;
    if (success)
    {
        device_stubs.info.end_success_count++;
    }
    else
    {
        device_stubs.info.end_fail_count++;
    }
}

const struct pouch_endpoint pouch_device_endpoint_info = {
    .start = d_info_start,
    .send = d_info_send,
    .end = d_info_end,
};

/* SERVER_CERT: device receives from broker (receiver) */

static int d_server_cert_start(struct pouch_bearer *bearer)
{
    (void) bearer;
    return receiver_start_helper(&device_stubs.server_cert);
}

static int d_server_cert_recv(struct pouch_bearer *bearer, const void *buf, size_t len)
{
    (void) bearer;
    return receiver_push(&device_stubs.server_cert, buf, len);
}

static void d_server_cert_end(struct pouch_bearer *bearer, bool success)
{
    (void) bearer;
    if (success)
    {
        device_stubs.server_cert.end_success_count++;
    }
    else
    {
        device_stubs.server_cert.end_fail_count++;
    }
}

const struct pouch_endpoint pouch_device_endpoint_server_cert = {
    .start = d_server_cert_start,
    .recv = d_server_cert_recv,
    .end = d_server_cert_end,
};

/* DEVICE_CERT: device sends to broker (sender) */

static int d_device_cert_start(struct pouch_bearer *bearer)
{
    (void) bearer;
    return sender_start_helper(&device_stubs.device_cert);
}

static enum pouch_result d_device_cert_send(struct pouch_bearer *bearer, void *dst, size_t *dst_len)
{
    (void) bearer;
    return sender_fill(&device_stubs.device_cert, dst, dst_len);
}

static void d_device_cert_end(struct pouch_bearer *bearer, bool success)
{
    (void) bearer;
    if (success)
    {
        device_stubs.device_cert.end_success_count++;
    }
    else
    {
        device_stubs.device_cert.end_fail_count++;
    }
}

const struct pouch_endpoint pouch_device_endpoint_device_cert = {
    .start = d_device_cert_start,
    .send = d_device_cert_send,
    .end = d_device_cert_end,
};

/* DOWNLINK: device receives from broker (receiver) */

static int d_downlink_start(struct pouch_bearer *bearer)
{
    (void) bearer;
    return receiver_start_helper(&device_stubs.downlink);
}

static int d_downlink_recv(struct pouch_bearer *bearer, const void *buf, size_t len)
{
    (void) bearer;
    return receiver_push(&device_stubs.downlink, buf, len);
}

static void d_downlink_end(struct pouch_bearer *bearer, bool success)
{
    (void) bearer;
    if (success)
    {
        device_stubs.downlink.end_success_count++;
    }
    else
    {
        device_stubs.downlink.end_fail_count++;
    }
}

const struct pouch_endpoint pouch_device_endpoint_downlink = {
    .start = d_downlink_start,
    .recv = d_downlink_recv,
    .end = d_downlink_end,
};

/* UPLINK: device sends to broker (sender) */

static int d_uplink_start(struct pouch_bearer *bearer)
{
    (void) bearer;
    return sender_start_helper(&device_stubs.uplink);
}

static enum pouch_result d_uplink_send(struct pouch_bearer *bearer, void *dst, size_t *dst_len)
{
    (void) bearer;
    return sender_fill(&device_stubs.uplink, dst, dst_len);
}

static void d_uplink_end(struct pouch_bearer *bearer, bool success)
{
    (void) bearer;
    if (success)
    {
        device_stubs.uplink.end_success_count++;
    }
    else
    {
        device_stubs.uplink.end_fail_count++;
    }
}

const struct pouch_endpoint pouch_device_endpoint_uplink = {
    .start = d_uplink_start,
    .send = d_uplink_send,
    .end = d_uplink_end,
};
