/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "uart.h"
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>

#include <pouch/transport/serial/broker.h>
#include <pouch/transport/uart/broker.h>

#include <string.h>

LOG_MODULE_REGISTER(pouch_uart, CONFIG_POUCH_UART_LOG_LEVEL);

#define RX_TIMEOUT_US (5 * USEC_PER_MSEC)

enum flags
{
    FLAG_TX_BUSY,
    FLAG_RX_DATA_REQUESTED,
};

static void tx_work_handler(struct k_work *work)
{
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct pouch_uart *ctx = CONTAINER_OF(dwork, struct pouch_uart, tx_work);

    if (atomic_test_and_set_bit(&ctx->flags, FLAG_TX_BUSY))
    {
        return;
    }

    size_t len = ctx->callbacks->tx_fill(ctx, &ctx->tx_buf[1]);
    if (len == 0)
    {
        LOG_DBG("No data");
        atomic_clear_bit(&ctx->flags, FLAG_TX_BUSY);
        return;
    }

    ctx->tx_buf[0] = (uint8_t) len;

    int err = uart_tx(ctx->uart, ctx->tx_buf, 1 + len, SYS_FOREVER_US);
    if (err)
    {
        LOG_ERR("uart_tx failed: %d", err);
        atomic_clear_bit(&ctx->flags, FLAG_TX_BUSY);
    }

    LOG_DBG("TX %u bytes", 1 + len);
}

static int rx_enable(struct pouch_uart *ctx)
{
    uint8_t *buf;
    size_t size = ring_buf_put_claim(&ctx->rx.buf, &buf, CONFIG_POUCH_UART_FRAME_SIZE + 1);
    if (size == 0)
    {
        return -ENOMEM;
    }

    int err = uart_rx_enable(ctx->uart, buf, size, RX_TIMEOUT_US);
    if (err)
    {
        LOG_ERR("Unable to enable rx: %d", err);
        return err;
    }

    atomic_add(&ctx->rx.claimed, size);
    return 0;
}

static void setup_rx_buf(struct pouch_uart *ctx)
{
    uint8_t *buf;
    size_t size = ring_buf_put_claim(&ctx->rx.buf, &buf, CONFIG_POUCH_UART_FRAME_SIZE + 1);
    if (size == 0)
    {
        atomic_set_bit(&ctx->flags, FLAG_RX_DATA_REQUESTED);
        return;
    }

    int err = uart_rx_buf_rsp(ctx->uart, buf, size);
    if (err)
    {
        atomic_set_bit(&ctx->flags, FLAG_RX_DATA_REQUESTED);
        return;
    }

    atomic_add(&ctx->rx.claimed, size);
}

static void process_rx_bytes(struct pouch_uart *ctx, size_t len)
{
    ring_buf_put_finish(&ctx->rx.buf, len);
    k_work_submit(&ctx->rx.work);

    uint8_t *dummy;
    atomic_sub(&ctx->rx.claimed, len);

    // the ring buffer will put the rest of the buffer back into an unclaimed state.
    // Our driver is still using it, so we need to reclaim it:
    size_t reclaimed = 0;
    while (reclaimed < ctx->rx.claimed)
    {
        size_t claimed = ring_buf_put_claim(&ctx->rx.buf, &dummy, ctx->rx.claimed - reclaimed);
        reclaimed += claimed;
        if (claimed == 0)
        {
            LOG_ERR("Unable to reclaim all data (missing %ld)", ctx->rx.claimed - reclaimed);
            atomic_set(&ctx->rx.claimed, reclaimed);
            return;
        }
    }
}

static void rx_process(struct pouch_uart *ctx)
{
    uint8_t len;
    size_t size = ring_buf_peek(&ctx->rx.buf, &len, 1);
    if (size == 0)
    {
        return;
    }

    size = ring_buf_size_get(&ctx->rx.buf);
    if (size < 1 + len)
    {
        // not enough data available
        return;
    }

    // commit to reading the data:
    ring_buf_get(&ctx->rx.buf, &len, 1);

    uint8_t frame[CONFIG_POUCH_UART_FRAME_SIZE];

    size = ring_buf_get(&ctx->rx.buf, frame, len);
    if (size != len)
    {
        LOG_ERR("Unexpected length read: %u != %u", size, len);
        return;
    }

    int err = ctx->callbacks->rx(ctx, frame, len);
    if (err)
    {
        LOG_ERR("RX failed: %d", err);
    }

    // tail recursion to pull more frames:
    rx_process(ctx);
}

static void rx_work_handler(struct k_work *work)
{
    struct pouch_uart *ctx = CONTAINER_OF(work, struct pouch_uart, rx.work);

    rx_process(ctx);

    if (atomic_test_and_clear_bit(&ctx->flags, FLAG_RX_DATA_REQUESTED))
    {
        setup_rx_buf(ctx);
    }
}

static void uart_callback(const struct device *dev, struct uart_event *evt, void *user_data)
{
    struct pouch_uart *ctx = user_data;

    switch (evt->type)
    {
        case UART_TX_DONE:
            // LOG_HEXDUMP_DBG(evt->data.tx.buf, evt->data.tx.len, "TX");
            atomic_clear_bit(&ctx->flags, FLAG_TX_BUSY);
            k_work_reschedule(&ctx->tx_work, K_MSEC(CONFIG_POUCH_UART_INTERFRAME_DELAY));
            break;
        case UART_TX_ABORTED:
            LOG_WRN("TX aborted");
            atomic_clear_bit(&ctx->flags, FLAG_TX_BUSY);
            break;
        case UART_RX_RDY:
            LOG_INF("RX %u bytes", evt->data.rx.len);
            // LOG_HEXDUMP_DBG(evt->data.rx.buf + evt->data.rx.offset, evt->data.rx.len, "RX");
            process_rx_bytes(ctx, evt->data.rx.len);
            break;
        case UART_RX_BUF_REQUEST:
            setup_rx_buf(ctx);
            break;
        case UART_RX_BUF_RELEASED:
            break;
        case UART_RX_DISABLED:
            LOG_WRN("RX disabled, re-enabling");
            rx_enable(ctx);
            break;
        case UART_RX_STOPPED:
            LOG_WRN("RX stopped");
            break;
        default:
            break;
    }
}

int pouch_uart_init(struct pouch_uart *ctx)
{
    if (!device_is_ready(ctx->uart))
    {
        LOG_ERR("UART device %p not ready", ctx->uart);
        return -ENODEV;
    }

    if (ctx->callbacks == NULL || ctx->callbacks->rx == NULL || ctx->callbacks->tx_fill == NULL)
    {
        LOG_ERR("UART callbacks missing");
        return -EINVAL;
    }

    k_work_init(&ctx->rx.work, rx_work_handler);
    ring_buf_init(&ctx->rx.buf, sizeof(ctx->rx.data), ctx->rx.data);

    int err = uart_callback_set(ctx->uart, uart_callback, ctx);
    if (err)
    {
        LOG_ERR("uart_callback_set failed: %d", err);
        return err;
    }

    k_work_init_delayable(&ctx->tx_work, tx_work_handler);

    err = rx_enable(ctx);
    if (err)
    {
        LOG_ERR("uart_rx_enable failed (inst %p): %d", ctx, err);
        return err;
    }

    LOG_DBG("Initialized UART broker (inst %p)", ctx);

    return 0;
}

void pouch_uart_ready(struct pouch_uart *ctx)
{
    k_work_reschedule(&ctx->tx_work, K_NO_WAIT);
}
