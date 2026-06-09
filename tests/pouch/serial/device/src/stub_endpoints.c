/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Stub endpoint implementations for device-side serial transport tests.
 *
 * Each of the 5 device endpoints is replaced by a simple stub that records
 * lifecycle events and either feeds canned data (senders) or captures received
 * data (receivers). The test accesses the stubs through the global
 * device_stubs pointer.
 */

#include "stub_endpoints.h"
#include "transport/bearer.h"

#include <string.h>

/* ---- Generic helpers ----------------------------------------------------- */

void stub_sender_reset(struct stub_sender *s)
{
    s->tx_len = 0;
    s->tx_offset = 0;
    s->start_count = 0;
    s->end_success_count = 0;
    s->end_fail_count = 0;
    s->start_err = 0;
    s->bearer = NULL;
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
    size_t remaining = s->tx_len - s->tx_offset;

    if (*dst_len > remaining)
    {
        *dst_len = remaining;
    }

    memcpy(dst, &s->tx_buf[s->tx_offset], *dst_len);
    s->tx_offset += *dst_len;

    return (s->tx_offset >= s->tx_len) ? POUCH_NO_MORE_DATA : POUCH_MORE_DATA;
}

void stub_receiver_reset(struct stub_receiver *r)
{
    r->rx_len = 0;
    r->start_count = 0;
    r->end_success_count = 0;
    r->end_fail_count = 0;
    r->start_err = 0;
    r->recv_err = 0;
}

static void receiver_push(struct stub_receiver *r, const void *buf, size_t len)
{
    size_t space = STUB_EP_BUF_SIZE - r->rx_len;

    if (len > space)
    {
        len = space;
    }

    memcpy(&r->rx_buf[r->rx_len], buf, len);
    r->rx_len += len;
}

/* ---- Device endpoint stubs ----------------------------------------------- */

struct device_stubs device_stubs;

/* INFO: device sends to broker (sender) */

static int d_info_start(struct pouch_bearer *bearer)
{
    device_stubs.info.bearer = bearer;
    device_stubs.info.tx_offset = 0;
    device_stubs.info.start_count++;
    return device_stubs.info.start_err;
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
    device_stubs.server_cert.rx_len = 0;
    device_stubs.server_cert.start_count++;
    return device_stubs.server_cert.start_err;
}

static int d_server_cert_recv(struct pouch_bearer *bearer, const void *buf, size_t len)
{
    (void) bearer;
    if (device_stubs.server_cert.recv_err != 0)
    {
        return device_stubs.server_cert.recv_err;
    }
    receiver_push(&device_stubs.server_cert, buf, len);
    return 0;
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
    device_stubs.device_cert.bearer = bearer;
    device_stubs.device_cert.tx_offset = 0;
    device_stubs.device_cert.start_count++;
    return device_stubs.device_cert.start_err;
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
    device_stubs.downlink.rx_len = 0;
    device_stubs.downlink.start_count++;
    return device_stubs.downlink.start_err;
}

static int d_downlink_recv(struct pouch_bearer *bearer, const void *buf, size_t len)
{
    (void) bearer;
    if (device_stubs.downlink.recv_err != 0)
    {
        return device_stubs.downlink.recv_err;
    }
    receiver_push(&device_stubs.downlink, buf, len);
    return 0;
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
    device_stubs.uplink.bearer = bearer;
    device_stubs.uplink.tx_offset = 0;
    device_stubs.uplink.start_count++;
    return device_stubs.uplink.start_err;
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
