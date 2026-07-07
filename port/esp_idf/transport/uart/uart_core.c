/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "uart_core.h"

#include <errno.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <pouch/port.h>

static const char *TAG = "pouch_uart";

enum uart_flags
{
    /** TX drain is in progress (guards re-entrancy across task contexts). */
    FLAG_TX_BUSY,
    /** Another frame became pending while TX was busy; re-drain when done. */
    FLAG_TX_PENDING,
};

// The interframe delay can be lower than a tick, but we'll still need to respect it. We'll yield
// for the as long as we can, then busy wait for the remained. The busy wait will always be less
// than a full tick.
static void interframe_delay(void)
{
    if (CONFIG_POUCH_UART_INTERFRAME_DELAY_MS == 0)
    {
        return;
    }

    int64_t start = esp_timer_get_time();
    vTaskDelay(pdMS_TO_TICKS(CONFIG_POUCH_UART_INTERFRAME_DELAY_MS));
    int32_t elapsed = esp_timer_get_time() - start;
    int32_t remaining_us = (CONFIG_POUCH_UART_INTERFRAME_DELAY_MS * 1000) - elapsed;
    if (remaining_us > 0)
    {
        esp_rom_delay_us(remaining_us);
    }
}

static void drain_tx(struct pouch_uart *uart)
{
    while (true)
    {
        size_t len = uart->api->frame_get(uart, &uart->tx_buf[1], sizeof(uart->tx_buf) - 1);
        if (len == 0)
        {
            break;
        }

        uart->tx_buf[0] = (uint8_t) len;
        ESP_LOGD(TAG, "writing %u bytes", len + 1);

        int written = uart_write_bytes(uart->port, uart->tx_buf, 1 + len);
        if (written < 0)
        {
            ESP_LOGE(TAG, "uart_write_bytes failed");
            break;
        }

        // uart_write_bytes returns when the TX is scheduled, so we need to block until the TX is
        // done. This function appears to be synced to ticks, which adds a bit to our interframe
        // delay.
        uart_wait_tx_done(uart->port, pdMS_TO_TICKS(1000));

        interframe_delay();
    }
}

void pouch_uart_notify(struct pouch_uart *uart)
{
    if (uart == NULL || uart->api == NULL || uart->api->frame_get == NULL)
    {
        return;
    }

    pouch_atomic_set_bit(&uart->flags, FLAG_TX_PENDING);
    if (pouch_atomic_test_and_set_bit(&uart->flags, FLAG_TX_BUSY))
    {
        /* Another context is already draining; it will pick up our pending bit. */
        return;
    }

    do
    {
        pouch_atomic_clear_bit(&uart->flags, FLAG_TX_PENDING);
        drain_tx(uart);
    } while (pouch_atomic_test_and_clear_bit(&uart->flags, FLAG_TX_PENDING));

    pouch_atomic_clear_bit(&uart->flags, FLAG_TX_BUSY);
}

static void deliver_frame(struct pouch_uart *uart)
{
    int err = uart->api->recv(uart, uart->rx.buf, uart->rx.expected);
    if (err)
    {
        ESP_LOGE(TAG, "RX delivery failed: %d", err);
    }
}

static void process_rx(struct pouch_uart *uart)
{
    size_t available;
    if (uart_get_buffered_data_len(uart->port, &available) != ESP_OK)
    {
        return;
    }

    while (available > 0)
    {
        if (!uart->rx.expected)
        {
            uint8_t len;
            int n = uart_read_bytes(uart->port, &len, 1, 0);
            if (n != 1)
            {
                break;
            }
            available--;

            if (len == 0)
            {
                /* No-op: peer prompted an exchange without payload. */
                continue;
            }

// Compiler throws a fit about len never being larger than sizeof(uart->rx.buf) if we've
// configured the buffer to be larger than 255 bytes.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wtype-limits"
            if (len > sizeof(uart->rx.buf))
#pragma GCC diagnostic pop
            {
                ESP_LOGW(TAG, "Frame length %u exceeds buffer, resyncing", len);
                continue;
            }

            uart->rx.expected = len;
            uart->rx.len = 0;
        }

        size_t need = uart->rx.expected - uart->rx.len;
        size_t to_read = need < available ? need : available;

        int n = uart_read_bytes(uart->port, &uart->rx.buf[uart->rx.len], to_read, 0);
        if (n <= 0)
        {
            break;
        }
        uart->rx.len += n;
        available -= n;

        if (uart->rx.len >= uart->rx.expected)
        {
            ESP_LOG_BUFFER_HEX_LEVEL(TAG, uart->rx.buf, uart->rx.expected, ESP_LOG_DEBUG);
            deliver_frame(uart);
            uart->rx.expected = 0;
            uart->rx.len = 0;
        }
    }
}

static void uart_event_task(void *arg)
{
    struct pouch_uart *uart = arg;
    uart_event_t event;

    for (;;)
    {
        if (xQueueReceive(uart->event_queue, &event, portMAX_DELAY) != pdTRUE)
        {
            continue;
        }

        switch (event.type)
        {
            case UART_DATA:
                process_rx(uart);
                break;
            case UART_FIFO_OVF:
                ESP_LOGW(TAG, "HW FIFO overflow");
                uart_flush_input(uart->port);
                xQueueReset(uart->event_queue);
                break;
            case UART_BUFFER_FULL:
                ESP_LOGW(TAG, "ring buffer full");
                uart_flush_input(uart->port);
                xQueueReset(uart->event_queue);
                break;
            case UART_BREAK:
                ESP_LOGD(TAG, "break detected");
                break;
            case UART_PARITY_ERR:
                ESP_LOGW(TAG, "parity error");
                break;
            case UART_FRAME_ERR:
                ESP_LOGW(TAG, "frame error");
                break;
            default:
                break;
        }
    }
}

int pouch_uart_start(struct pouch_uart *uart, const struct pouch_uart_api *api, void *ctx)
{
    if (uart == NULL || api == NULL || api->recv == NULL || api->frame_get == NULL)
    {
        return -EINVAL;
    }

    memset(uart, 0, sizeof(*uart));
    uart->port = CONFIG_POUCH_UART_PORT_NUM;
    uart->api = api;
    uart->ctx = ctx;

    uart_config_t uart_config = {
        .baud_rate = CONFIG_POUCH_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    int err = uart_driver_install(uart->port,
                                  CONFIG_POUCH_UART_RX_BUFFER_SIZE,
                                  CONFIG_POUCH_UART_TX_BUFFER_SIZE,
                                  CONFIG_POUCH_UART_EVENT_QUEUE_DEPTH,
                                  &uart->event_queue,
                                  0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
        return -EIO;
    }

    err = uart_param_config(uart->port, &uart_config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "uart_param_config failed: %s", esp_err_to_name(err));
        goto err_driver;
    }

    err = uart_set_pin(uart->port,
                       CONFIG_POUCH_UART_TX_PIN,
                       CONFIG_POUCH_UART_RX_PIN,
                       UART_PIN_NO_CHANGE,
                       UART_PIN_NO_CHANGE);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "uart_set_pin failed: %s", esp_err_to_name(err));
        goto err_driver;
    }

    BaseType_t ret = xTaskCreate(uart_event_task,
                                 "pouch_uart_rx",
                                 CONFIG_POUCH_UART_TASK_STACK_SIZE,
                                 uart,
                                 CONFIG_POUCH_UART_TASK_PRIORITY,
                                 &uart->task);
    if (ret != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create RX task");
        err = -ENOMEM;
        goto err_driver;
    }

    ESP_LOGD(TAG,
             "UART%d ready (tx=%d rx=%d baud=%d)",
             uart->port,
             CONFIG_POUCH_UART_TX_PIN,
             CONFIG_POUCH_UART_RX_PIN,
             CONFIG_POUCH_UART_BAUD_RATE);

    return 0;

err_driver:
    uart_driver_delete(uart->port);
    return err < 0 ? err : -EIO;
}
