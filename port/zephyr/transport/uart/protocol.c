/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(serial_device);

#include <string.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/util.h>

#include "protocol.h"

#define RX_BUF_SIZE 128

static uint8_t rx_double_buf[2][RX_BUF_SIZE];
static uint8_t rx_buf_idx;

/* RX reassembly state */
static uint8_t rx_frame_buf[SERIAL_DATA_MAXLEN];
static size_t rx_frame_pos;
static size_t rx_frame_len;
static enum serial_channel rx_frame_ch;
static bool rx_have_header;

static struct uart_transport
{
    const struct device *dev;
    struct
    {
        serial_recv_cmd_t cmd_cb;
        serial_recv_data_t data_cb;
    } channels[SERIAL_CHANNELS];
} transport_ctx;

static void rx_process(struct uart_transport *ctx, const uint8_t *data, size_t len)
{
    while (len > 0)
    {
        if (!rx_have_header)
        {
            /* Need 2 bytes for the header */
            if (rx_frame_pos == 0 && len >= 2)
            {
                uint16_t hdr = ((uint16_t) data[0] << 8) | data[1];
                rx_frame_ch = (enum serial_channel)(hdr >> 12);
                rx_frame_len = hdr & 0x0FFF;
                rx_frame_pos = 0;
                rx_have_header = true;
                data += 2;
                len -= 2;
            }
            else if (rx_frame_pos == 0 && len == 1)
            {
                /* Got only first header byte, stash it */
                rx_frame_buf[0] = data[0];
                rx_frame_pos = 1;
                return;
            }
            else if (rx_frame_pos == 1)
            {
                /* Second header byte */
                uint16_t hdr = ((uint16_t) rx_frame_buf[0] << 8) | data[0];
                rx_frame_ch = (enum serial_channel)(hdr >> 12);
                rx_frame_len = hdr & 0x0FFF;
                rx_frame_pos = 0;
                rx_have_header = true;
                data += 1;
                len -= 1;
            }
        }

        if (!rx_have_header)
        {
            return;
        }

        /* If this is a zero-length frame, treat as a command */
        if (rx_frame_len == 0)
        {
            /* TODO: decode command from channel */
            rx_have_header = false;
            rx_frame_pos = 0;
            continue;
        }

        /* Accumulate payload */
        size_t remaining = rx_frame_len - rx_frame_pos;
        size_t to_copy = MIN(remaining, len);

        memcpy(rx_frame_buf + rx_frame_pos, data, to_copy);
        rx_frame_pos += to_copy;
        data += to_copy;
        len -= to_copy;

        if (rx_frame_pos == rx_frame_len)
        {
            /* Complete frame received */
            if (rx_frame_ch < SERIAL_CHANNELS && ctx->channels[rx_frame_ch].data_cb)
            {
                ctx->channels[rx_frame_ch].data_cb(rx_frame_ch, rx_frame_buf, rx_frame_len);
            }
            rx_have_header = false;
            rx_frame_pos = 0;
        }
    }
}

static void uart_callback(const struct device *dev, struct uart_event *evt, void *user_data)
{
    struct uart_transport *ctx = user_data;
    int rc;

    switch (evt->type)
    {
        case UART_TX_DONE:
            LOG_DBG("TX complete (%d bytes)", evt->data.tx.len);
            break;

        case UART_TX_ABORTED:
            LOG_WRN("TX aborted");
            break;

        case UART_RX_RDY:
            LOG_DBG("RX ready: %u bytes", evt->data.rx.len);
            rx_process(ctx, evt->data.rx.buf + evt->data.rx.offset, evt->data.rx.len);
            break;

        case UART_RX_BUF_REQUEST:
            rc = uart_rx_buf_rsp(dev, rx_double_buf[rx_buf_idx], RX_BUF_SIZE);
            if (rc != 0)
            {
                LOG_ERR("Failed to provide RX buffer: %d", rc);
            }
            rx_buf_idx = rx_buf_idx ? 0 : 1;
            break;

        case UART_RX_BUF_RELEASED:
            break;

        case UART_RX_DISABLED:
            LOG_WRN("RX disabled - re-enabling");
            rc = uart_rx_enable(dev, rx_double_buf[0], RX_BUF_SIZE, 100);
            if (rc != 0)
            {
                LOG_ERR("Failed to re-enable RX: %d", rc);
            }
            rx_buf_idx = 1;
            break;

        case UART_RX_STOPPED:
            LOG_ERR("RX stopped, reason: %d", evt->data.rx_stop.reason);
            break;

        default:
            LOG_WRN("Unhandled UART event: %d", evt->type);
            break;
    }
}

int serial_init(const struct device *device)
{
    int rc;

    transport_ctx.dev = device;
    uart_callback_set(device, uart_callback, &transport_ctx);

    /* Start async RX with double-buffering */
    rx_buf_idx = 1;
    rc = uart_rx_enable(device, rx_double_buf[0], RX_BUF_SIZE, 100);
    if (rc != 0)
    {
        LOG_ERR("Failed to enable UART RX: %d", rc);
    }

    return rc;
}

int serial_ch_init(enum serial_channel ch, serial_recv_data_t recv_data, serial_recv_cmd_t recv_cmd)
{
    if (ch >= SERIAL_CHANNELS)
    {
        return -EINVAL;
    }
    transport_ctx.channels[ch].data_cb = recv_data;
    transport_ctx.channels[ch].cmd_cb = recv_cmd;
    return 0;
}

/* TX buffer: 2-byte header + max payload */
static uint8_t tx_frame_buf[2 + SERIAL_DATA_MAXLEN];

int serial_send(enum serial_channel ch, const void *data, size_t len)
{
    if (len > SERIAL_DATA_MAXLEN)
    {
        LOG_ERR("TX payload too large: %zu", len);
        return -EINVAL;
    }

    uint16_t hdr = ((uint16_t) ch << 12) | (len & 0x0FFF);
    tx_frame_buf[0] = (uint8_t) (hdr >> 8);
    tx_frame_buf[1] = (uint8_t) (hdr & 0xFF);
    memcpy(tx_frame_buf + 2, data, len);

    int rc = uart_tx(transport_ctx.dev, tx_frame_buf, 2 + len, SYS_FOREVER_US);
    if (rc != 0)
    {
        LOG_ERR("Failed to send to uart: %d", rc);
    }

    return rc;
}

int serial_ack(bool success)
{
    /* FIXME: send ack? */
    return 0;
}
