/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "transport/endpoints/endpoint.h"

#define STUB_EP_BUF_SIZE 512

struct stub_sender
{
    uint8_t tx_buf[STUB_EP_BUF_SIZE];
    size_t tx_len;
    size_t tx_offset;

    int start_count;
    int end_success_count;
    int end_fail_count;

    /** If non-zero, start() returns this error code. */
    int start_err;

    /** If true, send() returns POUCH_ERROR. */
    bool send_err;

    /** Bearer pointer captured during start(). */
    struct pouch_bearer *bearer;
};

struct stub_receiver
{
    uint8_t rx_buf[STUB_EP_BUF_SIZE];
    size_t rx_len;

    int start_count;
    int end_success_count;
    int end_fail_count;

    /** If non-zero, start() returns this error code. */
    int start_err;
    /** If non-zero, recv() returns this error code. */
    int recv_err;

    /** Bearer pointer captured during start(). */
    struct pouch_bearer *bearer;
};

struct broker_stubs
{
    struct stub_receiver info;
    struct stub_sender server_cert;
    struct stub_receiver device_cert;
    struct stub_sender downlink;
    struct stub_receiver uplink;
};

extern struct broker_stubs broker_stubs;

void stub_sender_reset(struct stub_sender *s);
void stub_sender_set_data(struct stub_sender *s, const uint8_t *data, size_t len);
void stub_receiver_reset(struct stub_receiver *r);
