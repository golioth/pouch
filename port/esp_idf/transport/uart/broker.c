/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Pouch UART broker transport adapter (ESP-IDF)
 *
 * The broker side runs on the gateway and drives the Pouch exchange sequence
 * with a device over a point-to-point UART link. It wraps the shared UART
 * core (@ref uart_core.c) and bridges to the broker-side serial transport
 * module (pouch/include/pouch/transport/serial/broker.h).
 *
 * The adapter creates a @ref pouch_serial_broker at startup and exposes
 * @ref pouch_uart_broker_start to begin an exchange. Outbound frames flow
 * through the broker's @c ready callback into the core; inbound frames are
 * delivered to @ref pouch_serial_broker_recv.
 *
 * Unlike the SPI broker, no data-ready interrupt line or polling is needed:
 * UART is full-duplex, so the device spontaneously transmits its responses,
 * which the core delivers as they arrive.
 */

#include "uart_core.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_log.h"

#include <pouch/port.h>
#include <pouch/transport/serial/broker.h>
#include <pouch/transport/uart/broker.h>

static const char *TAG = "pouch_uart_brk";

static struct pouch_uart uart;
static struct pouch_serial_broker *broker;

static int broker_recv(struct pouch_uart *u, const uint8_t *frame, size_t len)
{
    return pouch_serial_broker_recv(broker, frame, len);
}

static size_t broker_frame_get(struct pouch_uart *u, uint8_t *buf, size_t maxlen)
{
    return pouch_serial_broker_frame_get(broker, buf, maxlen);
}

static const struct pouch_uart_api broker_api = {
    .recv = broker_recv,
    .frame_get = broker_frame_get,
};

static void adapter_ready(const struct pouch_serial_broker *b)
{
    pouch_uart_notify(&uart);
}

static void adapter_end(const struct pouch_serial_broker *b, bool success)
{
    if (success)
    {
        ESP_LOGD(TAG, "Exchange completed");
    }
    else
    {
        ESP_LOGW(TAG, "Exchange failed");
    }
}

static const struct pouch_serial_broker_adapter adapter = {
    .ready = adapter_ready,
    .end = adapter_end,
};

static void pouch_uart_broker_init(void)
{
    int err = pouch_uart_start(&uart, &broker_api, NULL);
    if (err)
    {
        ESP_LOGE(TAG, "Failed to start UART broker transport: %d", err);
        return;
    }

    broker = pouch_serial_broker_create(&adapter);
    if (broker == NULL)
    {
        ESP_LOGE(TAG, "Failed to create serial broker");
        return;
    }

    ESP_LOGI(TAG, "Pouch UART broker transport initialized");
}

POUCH_APPLICATION_STARTUP_HOOK(pouch_uart_broker_init);

void pouch_uart_broker_start(void)
{
    if (broker == NULL)
    {
        ESP_LOGE(TAG, "Broker not initialized");
        return;
    }

    pouch_serial_broker_start(broker);
}
