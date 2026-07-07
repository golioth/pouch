/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "uart.h"
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <pouch/transport/serial/device.h>

#include <string.h>

LOG_MODULE_REGISTER(pouch_uart_device, CONFIG_POUCH_UART_DEVICE_LOG_LEVEL);

#define DT_DRV_COMPAT golioth_pouch_device
#if DT_ANY_INST_ON_BUS_STATUS_OKAY(uart)

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) <= 1,
             "At most one golioth,pouch-device node is supported");

static size_t tx_buf_get(struct pouch_uart *uart, uint8_t *buf)
{
    return pouch_serial_device_frame_get(buf, CONFIG_POUCH_UART_FRAME_SIZE);
}

static int rx(struct pouch_uart *uart, const uint8_t *buf, size_t len)
{
    return pouch_serial_device_recv(buf, len);
}

static const struct pouch_uart_cb callbacks = {
    .tx_fill = tx_buf_get,
    .rx = rx,
};

#define UART_DEV_INIT(inst)                    \
    static struct pouch_uart uart_dev_##inst = \
        POUCH_UART_INIT(DEVICE_DT_GET(DT_PARENT(DT_DRV_INST(inst))), &callbacks);

DT_INST_FOREACH_STATUS_OKAY(UART_DEV_INIT)

#define UART_DEV_READY(inst) pouch_uart_ready(&uart_dev_##inst);

static void device_ready(void)
{
    DT_INST_FOREACH_STATUS_OKAY(UART_DEV_READY)
}

static int uart_device_init(void)
{
    pouch_serial_device_init(device_ready);
    return 0;
}

SYS_INIT(uart_device_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

#endif
