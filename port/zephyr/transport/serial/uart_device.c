/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/ring_buffer.h>

#include <pouch/transport/serial/device.h>

#include <errno.h>
#include <stdint.h>

LOG_MODULE_REGISTER(pouch_uart_device, CONFIG_POUCH_SERIAL_UART_DEVICE_LOG_LEVEL);

/*
 * Interrupt-driven UART device adapter for the Pouch Serial transport.
 *
 * The Pouch Serial core produces and consumes whole frames (a 1-byte serial
 * header plus payload). A UART is a raw byte stream, so this adapter adds a
 * minimal length-delimited framing to recover frame boundaries on the wire:
 *
 *     [ SOF=0xA5 ][ len_hi ][ len_lo ][ serial frame bytes ... ]
 *
 * This is the device-side adapter used for development and native_sim
 * end-to-end testing (over a host pty via the native-tty UART driver), where
 * the broker runs as a host process attached to the other end of the link.
 * The reliable, ordered link means no retransmission is required; the SOF byte
 * only provides cheap resynchronization if framing is ever lost.
 */

#define UART_FRAME_SOF 0xA5

#define POUCH_UART_NODE DT_CHOSEN(golioth_pouch_serial_uart)

BUILD_ASSERT(DT_NODE_EXISTS(POUCH_UART_NODE),
             "The chosen node golioth,pouch-serial-uart must reference a UART device");

static const struct device *const uart_dev = DEVICE_DT_GET(POUCH_UART_NODE);

RING_BUF_DECLARE(rx_ring, CONFIG_POUCH_SERIAL_UART_DEVICE_RX_RING_SIZE);
static K_SEM_DEFINE(rx_sem, 0, 1);

static K_MUTEX_DEFINE(tx_mutex);
static struct k_work_delayable tx_work;

static K_THREAD_STACK_DEFINE(rx_stack, CONFIG_POUCH_SERIAL_UART_DEVICE_RX_THREAD_STACK_SIZE);
static struct k_thread rx_thread;

static void uart_isr(const struct device *dev, void *user_data)
{
    ARG_UNUSED(user_data);

    while (uart_irq_update(dev) && uart_irq_rx_ready(dev))
    {
        uint8_t buf[64];
        int n = uart_fifo_read(dev, buf, sizeof(buf));
        if (n <= 0)
        {
            break;
        }

        uint32_t written = ring_buf_put(&rx_ring, buf, n);
        if (written < (uint32_t) n)
        {
            LOG_WRN("RX ring overflow, dropped %d bytes", n - (int) written);
        }
        k_sem_give(&rx_sem);
    }
}

/* Pull one byte from the RX ring, blocking until available. */
static uint8_t rx_byte(void)
{
    uint8_t b;

    while (ring_buf_get(&rx_ring, &b, 1) != 1)
    {
        k_sem_take(&rx_sem, K_FOREVER);
    }

    return b;
}

static void rx_thread_fn(void *a, void *b, void *c)
{
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);

    static uint8_t frame[CONFIG_POUCH_SERIAL_UART_DEVICE_FRAME_SIZE];

    while (true)
    {
        /* Scan for start of frame. */
        if (rx_byte() != UART_FRAME_SOF)
        {
            continue;
        }

        size_t len = ((size_t) rx_byte() << 8) | rx_byte();
        if (len == 0 || len > sizeof(frame))
        {
            LOG_WRN("Bad frame length %zu, resyncing", len);
            continue;
        }

        for (size_t i = 0; i < len; i++)
        {
            frame[i] = rx_byte();
        }

        int err = pouch_serial_device_recv(frame, len);
        if (err)
        {
            LOG_ERR("RX process failed: %d", err);
        }

        /* Receiving may have produced a response frame; kick the TX path. */
        k_work_reschedule(&tx_work, K_NO_WAIT);
    }
}

static void tx_process(struct k_work *work)
{
    ARG_UNUSED(work);

    uint8_t frame[CONFIG_POUCH_SERIAL_UART_DEVICE_FRAME_SIZE];

    k_mutex_lock(&tx_mutex, K_FOREVER);

    while (true)
    {
        size_t len = pouch_serial_device_frame_get(frame, sizeof(frame));
        if (len == 0)
        {
            break;
        }

        uart_poll_out(uart_dev, UART_FRAME_SOF);
        uart_poll_out(uart_dev, (uint8_t) (len >> 8));
        uart_poll_out(uart_dev, (uint8_t) (len & 0xff));
        for (size_t i = 0; i < len; i++)
        {
            uart_poll_out(uart_dev, frame[i]);
        }
    }

    k_mutex_unlock(&tx_mutex);
}

/* Serial core -> adapter: a frame is available to send. */
static void serial_ready_cb(void)
{
    k_work_reschedule(&tx_work, K_NO_WAIT);
}

static int pouch_uart_device_init(void)
{
    if (!device_is_ready(uart_dev))
    {
        LOG_ERR("UART device not ready");
        return -ENODEV;
    }

    k_work_init_delayable(&tx_work, tx_process);

    pouch_serial_device_init(serial_ready_cb);

    uart_irq_callback_user_data_set(uart_dev, uart_isr, NULL);
    uart_irq_rx_enable(uart_dev);

    k_thread_create(&rx_thread,
                    rx_stack,
                    K_THREAD_STACK_SIZEOF(rx_stack),
                    rx_thread_fn,
                    NULL,
                    NULL,
                    NULL,
                    CONFIG_POUCH_SERIAL_UART_DEVICE_RX_THREAD_PRIORITY,
                    0,
                    K_NO_WAIT);
    k_thread_name_set(&rx_thread, "pouch_uart_rx");

    LOG_DBG("UART device transport ready");
    return 0;
}

SYS_INIT(pouch_uart_device_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
