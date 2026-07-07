/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/ring_buffer.h>

#define POUCH_UART_RX_BUF_SIZE (1 + CONFIG_POUCH_UART_FRAME_SIZE)

#define POUCH_UART_INIT(device, cb) \
    {                               \
        .uart = device,             \
        .callbacks = cb,            \
    }

struct pouch_uart;

struct pouch_uart_cb
{
    size_t (*tx_fill)(struct pouch_uart *ctx, uint8_t *buf);
    int (*rx)(struct pouch_uart *ctx, const uint8_t *buf, size_t len);
};

struct pouch_uart
{
    const struct device *uart;
    const struct pouch_uart_cb *callbacks;
    struct k_work_delayable tx_work;
    atomic_t flags;

    uint8_t tx_buf[1 + CONFIG_POUCH_UART_FRAME_SIZE];

    struct
    {
        struct ring_buf buf;
        uint8_t data[2 * (1 + CONFIG_POUCH_UART_FRAME_SIZE)];
        atomic_t claimed;
        uint8_t expected;
        uint8_t len;
        struct k_work work;
    } rx;
};

int pouch_uart_init(struct pouch_uart *ctx);
void pouch_uart_ready(struct pouch_uart *ctx);
