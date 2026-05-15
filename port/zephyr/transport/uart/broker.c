/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/sys/util.h>
#include <zephyr/device.h>
#include "protocol.h"
#include "channels.h"
#include "transport/endpoints/broker/endpoints.h"

static struct channel channels[SERIAL_CHANNELS] = {
    [SERIAL_CH_INFO] = {.endpoint = &broker_endpoint_info},
    [SERIAL_CH_DEVICE_CERT] = {.endpoint = &broker_endpoint_device_cert},
    [SERIAL_CH_SERVER_CERT] = {.endpoint = &broker_endpoint_server_cert},
    [SERIAL_CH_UPLINK] = {.endpoint = &broker_endpoint_uplink},
    [SERIAL_CH_DOWNLINK] = {.endpoint = &broker_endpoint_downlink},
};

int pouch_serial_broker_init(const struct device *dev)
{
    // todo
    return 0;
}
