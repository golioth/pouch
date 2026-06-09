/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Stub endpoint implementations for broker-side serial transport tests.
 *
 * Each of the 5 broker endpoints is replaced by a simple stub that records
 * lifecycle events and either feeds canned data (senders) or captures received
 * data (receivers). The test accesses the stubs through the global
 * broker_stubs instance.
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
    s->send_err = false;
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
    if (s->send_err)
    {
        *dst_len = 0;
        return POUCH_ERROR;
    }

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
    r->bearer = NULL;
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

/* ---- Broker endpoint stubs ----------------------------------------------- */

struct broker_stubs broker_stubs;

/* INFO: broker receives from device (receiver) */

static int b_info_start(struct pouch_bearer *bearer)
{
    broker_stubs.info.bearer = bearer;
    broker_stubs.info.rx_len = 0;
    broker_stubs.info.start_count++;
    return broker_stubs.info.start_err;
}

static int b_info_recv(struct pouch_bearer *bearer, const void *buf, size_t len)
{
    (void) bearer;
    if (broker_stubs.info.recv_err != 0)
    {
        return broker_stubs.info.recv_err;
    }
    receiver_push(&broker_stubs.info, buf, len);
    return 0;
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
    broker_stubs.server_cert.bearer = bearer;
    broker_stubs.server_cert.tx_offset = 0;
    broker_stubs.server_cert.start_count++;
    return broker_stubs.server_cert.start_err;
}

static enum pouch_result b_server_cert_send(struct pouch_bearer *bearer, void *dst, size_t *dst_len)
{
    (void) bearer;
    return sender_fill(&broker_stubs.server_cert, dst, dst_len);
}

static void b_server_cert_end(struct pouch_bearer *bearer, bool success)
{
    (void) bearer;
    if (success)
    {
        broker_stubs.server_cert.end_success_count++;
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
    broker_stubs.device_cert.bearer = bearer;
    broker_stubs.device_cert.rx_len = 0;
    broker_stubs.device_cert.start_count++;
    return broker_stubs.device_cert.start_err;
}

static int b_device_cert_recv(struct pouch_bearer *bearer, const void *buf, size_t len)
{
    (void) bearer;
    if (broker_stubs.device_cert.recv_err != 0)
    {
        return broker_stubs.device_cert.recv_err;
    }
    receiver_push(&broker_stubs.device_cert, buf, len);
    return 0;
}

static void b_device_cert_end(struct pouch_bearer *bearer, bool success)
{
    (void) bearer;
    if (success)
    {
        broker_stubs.device_cert.end_success_count++;
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
    broker_stubs.downlink.bearer = bearer;
    broker_stubs.downlink.tx_offset = 0;
    broker_stubs.downlink.start_count++;
    return broker_stubs.downlink.start_err;
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
    broker_stubs.uplink.bearer = bearer;
    broker_stubs.uplink.rx_len = 0;
    broker_stubs.uplink.start_count++;
    return broker_stubs.uplink.start_err;
}

static int b_uplink_recv(struct pouch_bearer *bearer, const void *buf, size_t len)
{
    (void) bearer;
    if (broker_stubs.uplink.recv_err != 0)
    {
        return broker_stubs.uplink.recv_err;
    }
    receiver_push(&broker_stubs.uplink, buf, len);
    return 0;
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
