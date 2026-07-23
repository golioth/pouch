/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <pouch/transport/serial/device.h>
#include "serial.h"
#include "../endpoints/device/endpoints.h"

#include <stddef.h>
#include <stdint.h>

#define CHANNEL(_endpoint) {.endpoint = _endpoint}

static struct pouch_serial serial = {
    .channels =
        {
            [POUCH_SERIAL_CH_INFO] = CHANNEL(&pouch_device_endpoint_info),
#if defined(CONFIG_POUCH_ENCRYPTION_SAEAD)
            [POUCH_SERIAL_CH_SERVER_CERT] = CHANNEL(&pouch_device_endpoint_server_cert),
            [POUCH_SERIAL_CH_DEVICE_CERT] = CHANNEL(&pouch_device_endpoint_device_cert),
#endif
            [POUCH_SERIAL_CH_DOWNLINK] = CHANNEL(&pouch_device_endpoint_downlink),
            [POUCH_SERIAL_CH_UPLINK] = CHANNEL(&pouch_device_endpoint_uplink),
        },
};

static pouch_serial_device_ready_cb_t ready_cb;

static void ready(struct pouch_serial *s)
{
    if (ready_cb)
    {
        ready_cb();
    }
}

void pouch_serial_device_init(pouch_serial_device_ready_cb_t cb)
{
    ready_cb = cb;
    pouch_serial_init(&serial, ready, NULL);
}

int pouch_serial_device_recv(const void *frame, size_t len)
{
    return pouch_serial_recv(&serial, frame, len);
}

size_t pouch_serial_device_frame_get(uint8_t *buf, size_t maxlen)
{
    return pouch_serial_frame_get(&serial, buf, maxlen);
}
