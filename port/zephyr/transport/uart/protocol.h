/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include <stdlib.h>
#include <stdbool.h>

/** Max data length (limited to 12 bits) */
#define SERIAL_DATA_MAXLEN (0xfff)

enum serial_channel
{
    SERIAL_CH_INFO,
    SERIAL_CH_SERVER_CERT,
    SERIAL_CH_DEVICE_CERT,
    SERIAL_CH_UPLINK,
    SERIAL_CH_DOWNLINK,

    SERIAL_CHANNELS,
};

enum serial_cmd
{
    SERIAL_CMD_OPEN,
    SERIAL_CMD_CLOSE,
    SERIAL_CMD_ACK,
    SERIAL_CMD_NACK,
};

typedef int (*serial_recv_data_t)(enum serial_channel ch, const void *buf, size_t len);
typedef int (*serial_recv_cmd_t)(enum serial_channel ch, enum serial_cmd cmd);

int serial_init(enum serial_channel ch, serial_recv_data_t recv_data, serial_recv_cmd_t recv_cmd);
int serial_send_data(enum serial_channel ch, const void *data, size_t len);
int serial_send_cmd(enum serial_channel ch, enum serial_cmd);
