/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Shared UART core for the Pouch serial transport adapters.
 *
 * UART is a symmetric, full-duplex bus: both the device and the broker side
 * use the same wire format and driver logic. This core provides the common
 * UART initialization, an asynchronous RX event task that performs length
 * delimited framing, and a thread-safe TX path.
 *
 * The device (@ref device.c) and broker (@ref broker.c) adapters each
 * instantiate a @ref pouch_uart and supply a @ref pouch_uart_api vtable that
 * bridges to the shared serial transport module
 * (pouch/include/pouch/transport/serial).
 *
 * Wire format
 * -----------
 * Each Pouch serial frame is carried in a length-delimited UART frame:
 *
 *   byte 0:        payload length N (1..@ref POUCH_UART_FRAME_SIZE)
 *   bytes 1..N:    Pouch serial frame (1-byte header + payload)
 *
 * A length byte of 0 is a no-op (used to prompt the peer without data).
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/uart.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <pouch/port.h>

struct pouch_uart;

/**
 * Operations supplied by the device or broker adapter.
 *
 * The core calls these to hand received frames to the shared serial
 * transport module and to pull outbound frames from it.
 */
struct pouch_uart_api
{
    /**
     * Deliver a complete received frame to the serial transport layer.
     *
     * @param uart   UART core instance.
     * @param frame  Frame payload (the Pouch serial frame, without the
     *               UART length byte).
     * @param len    Frame payload length in bytes.
     *
     * @return 0 on success, negative error code on failure.
     */
    int (*recv)(struct pouch_uart *uart, const uint8_t *frame, size_t len);

    /**
     * Retrieve a frame to transmit.
     *
     * The core writes the returned bytes to the UART after prepending the
     * length byte, so @p buf must hold at most @p maxlen - 1 payload bytes.
     *
     * @param uart   UART core instance.
     * @param buf    Buffer to fill with the Pouch serial frame to send.
     * @param maxlen Capacity of @p buf (including room for the length byte).
     *
     * @return Number of payload bytes written to @p buf, or 0 if nothing
     *         is pending.
     */
    size_t (*frame_get)(struct pouch_uart *uart, uint8_t *buf, size_t maxlen);
};

/**
 * UART core instance. One is instantiated statically by each adapter.
 */
struct pouch_uart
{
    /** UART port number (e.g. @ref UART_NUM_1). */
    uart_port_t port;
    /** Event queue returned by @ref uart_driver_install. */
    QueueHandle_t event_queue;
    /** RX event task handle. */
    TaskHandle_t task;

    /** Adapter operations. */
    const struct pouch_uart_api *api;
    /** Optional adapter context (e.g. broker instance). */
    void *ctx;

    /** RX framing state. */
    struct
    {
        uint8_t buf[CONFIG_POUCH_UART_FRAME_SIZE];
        size_t len;
        uint8_t expected;
    } rx;

    /** TX scratch buffer (length byte + payload). */
    uint8_t tx_buf[1 + CONFIG_POUCH_UART_FRAME_SIZE];

    /** Internal flags (see enum @ref uart_flags). */
    pouch_atomic_t flags;
};

/**
 * Configure the UART driver and start the asynchronous RX task.
 *
 * @param uart  Core instance to initialize.
 * @param api   Adapter operations.
 * @param ctx   Optional adapter context stored in @ref pouch_uart.ctx.
 *
 * @return 0 on success, negative error code on failure.
 */
int pouch_uart_start(struct pouch_uart *uart, const struct pouch_uart_api *api, void *ctx);

/**
 * Signal that the serial transport has outbound data ready.
 *
 * Called by the adapter (from the device ready callback or the broker
 * adapter @c ready callback) whenever the shared serial module has a frame
 * to send. The core drains all pending frames at the next opportunity.
 * Safe to call from any task context.
 *
 * @param uart UART core instance.
 */
void pouch_uart_notify(struct pouch_uart *uart);
