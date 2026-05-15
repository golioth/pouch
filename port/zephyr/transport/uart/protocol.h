/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include <stdlib.h>
#include <stdbool.h>

enum serial_channel
{
    SERIAL_CH_ACK,
    SERIAL_CH_SERVER_CERT,
    SERIAL_CH_DEVICE_CERT,
    SERIAL_CH_UPLINK,
    SERIAL_CH_DOWNLINK,
    SERIAL_CH_INFO,
};

typedef int (*serial_recv_t)(const void *buf, size_t len);

int serial_init(enum serial_channel ch, serial_recv_t callback);
int serial_send(enum serial_channel ch, const void *data, size_t len);
int serial_ack(bool success);
