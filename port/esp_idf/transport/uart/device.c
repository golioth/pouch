/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Pouch UART device transport adapter (ESP-IDF)
 *
 * The device side runs on the node and communicates with a broker over a
 * point-to-point UART link. It is a thin wrapper around the shared UART core
 * (@ref uart_core.c) that bridges to the device-side serial transport module
 * (pouch/include/pouch/transport/serial/device.h).
 *
 * The adapter initializes the UART core at application startup and registers
 * a ready callback with the serial transport. Whenever the transport has an
 * outbound frame, the core transmits it; inbound frames are delivered to
 * @ref pouch_serial_device_recv.
 */

#include "uart_core.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_log.h"

#include <pouch/port.h>
#include <pouch/transport/serial/device.h>

static const char *TAG = "pouch_uart_dev";

static struct pouch_uart uart;

static int device_recv(struct pouch_uart *u, const uint8_t *frame, size_t len)
{
    return pouch_serial_device_recv(frame, len);
}

static size_t device_frame_get(struct pouch_uart *u, uint8_t *buf, size_t maxlen)
{
    return pouch_serial_device_frame_get(buf, maxlen);
}

static const struct pouch_uart_api device_api = {
    .recv = device_recv,
    .frame_get = device_frame_get,
};

static void device_ready(void)
{
    pouch_uart_notify(&uart);
}

static void pouch_uart_device_init(void)
{
    pouch_serial_device_init(device_ready);

    int err = pouch_uart_start(&uart, &device_api, NULL);
    if (err)
    {
        ESP_LOGE(TAG, "Failed to start UART device transport: %d", err);
        return;
    }

    ESP_LOGI(TAG, "Pouch UART device transport initialized");
}

POUCH_APPLICATION_STARTUP_HOOK(pouch_uart_device_init);
