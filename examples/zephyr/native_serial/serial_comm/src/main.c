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
        printk("Server: uart1 device not ready\n");
        return -1;
    }

    printk("Server: waiting for data on uart1\n");

    unsigned char buf[128];
    int idx = 0;

    while (1)
    {
        unsigned char c;

        if (uart_poll_in(uart, &c) == 0)
        {
            if (c == '\n' || idx >= (int) (sizeof(buf) - 1))
            {
                buf[idx] = '\0';
                printk("RECEIVED: %s\n", buf);
                idx = 0;
            }
            else
            {
                buf[idx++] = c;
            }
        }

        k_sleep(K_MSEC(1));
    }

    return 0;
}
