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

    /* Error injection: return this from start(), 0 = success. */
    int start_err;
    /* Error injection: return POUCH_ERROR after N successful send() calls,
     * -1 = never. */
    int send_err_after;
    int send_count;
};

struct stub_receiver
{
    uint8_t rx_buf[STUB_EP_BUF_SIZE];
    size_t rx_len;

    int start_count;
    int end_success_count;
    int end_fail_count;

    /* Error injection: return this from start(), 0 = success. */
    int start_err;
    /* Error injection: return this from recv(), 0 = success. */
    int recv_err;
};

struct broker_stubs
{
    struct stub_receiver info;
    struct stub_sender server_cert;
    struct stub_receiver device_cert;
    struct stub_sender downlink;
    struct stub_receiver uplink;
};

struct device_stubs
{
    struct stub_sender info;
    struct stub_receiver server_cert;
    struct stub_sender device_cert;
    struct stub_receiver downlink;
    struct stub_sender uplink;
};

extern struct broker_stubs broker_stubs;
extern struct device_stubs device_stubs;

void stubs_reset(void);
void stub_sender_set_data(struct stub_sender *s, const uint8_t *data, size_t len);
