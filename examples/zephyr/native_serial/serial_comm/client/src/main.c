/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>

int main(void)
{
    const struct device *uart = DEVICE_DT_GET(DT_NODELABEL(uart1));

    if (!device_is_ready(uart))
    {
        printk("Client: uart1 device not ready\n");
        return -1;
    }

    printk("Client: sending data on uart1\n");

    const char *msg = "Hello from client!\n";

    for (int i = 0; msg[i] != '\0'; i++)
    {
        uart_poll_out(uart, msg[i]);
    }

    printk("Client: done sending\n");

    return 0;
}
