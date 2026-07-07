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

#include <pouch/transport/serial/broker.h>
#include <pouch/transport/uart/broker.h>

#include <string.h>

LOG_MODULE_REGISTER(pouch_uart_broker, LOG_LEVEL_DBG);

#define WATCHDOG_TIMEOUT K_MSEC(1000)

#define DT_DRV_COMPAT golioth_pouch_broker
#define ON_UART_BUS(inst, fn) IF_ENABLED(DT_INST_ON_BUS(inst, uart), (fn(inst)))

struct pouch_uart_broker
{
    struct pouch_serial_broker *broker;
    struct pouch_serial_broker_adapter adapter;
    struct k_work_delayable watchdog;
    struct pouch_uart uart;
};

static void watchdog_timeout(struct k_work *work)
{
    struct pouch_uart_broker *transport =
        CONTAINER_OF(k_work_delayable_from_work(work), struct pouch_uart_broker, watchdog);
    LOG_DBG("");
    pouch_serial_broker_notify(transport->broker);
    k_work_reschedule(&transport->watchdog, WATCHDOG_TIMEOUT);
}

static void adapter_ready(const struct pouch_serial_broker *broker)
{
    const struct pouch_serial_broker_adapter *adapter = pouch_serial_broker_adapter_get(broker);
    struct pouch_uart_broker *transport = CONTAINER_OF(adapter, struct pouch_uart_broker, adapter);

    pouch_uart_ready(&transport->uart);
}

static void adapter_end(const struct pouch_serial_broker *broker, bool success)
{
    if (success)
    {
        LOG_DBG("Exchange completed");
    }
    else
    {
        LOG_WRN("Exchange failed");
    }
}

static size_t tx_buf_get(struct pouch_uart *uart, uint8_t *buf)
{
    struct pouch_uart_broker *transport = CONTAINER_OF(uart, struct pouch_uart_broker, uart);
    k_work_reschedule(&transport->watchdog, WATCHDOG_TIMEOUT);
    return pouch_serial_broker_frame_get(transport->broker, buf, CONFIG_POUCH_UART_FRAME_SIZE);
}

static int rx(struct pouch_uart *uart, const uint8_t *buf, size_t len)
{
    struct pouch_uart_broker *transport = CONTAINER_OF(uart, struct pouch_uart_broker, uart);
    k_work_reschedule(&transport->watchdog, WATCHDOG_TIMEOUT);

    return pouch_serial_broker_recv(transport->broker, buf, len);
}

static const struct pouch_uart_cb broker_cb = {
    .tx_fill = tx_buf_get,
    .rx = rx,
};


#define INST_DEFINE(inst)                                                                 \
    static struct pouch_uart_broker uart_broker_##inst = {                                \
        .uart = POUCH_UART_INIT(DEVICE_DT_GET(DT_PARENT(DT_DRV_INST(inst))), &broker_cb), \
        .adapter =                                                                        \
            {                                                                             \
                .ready = adapter_ready,                                                   \
                .end = adapter_end,                                                       \
            },                                                                            \
    };
#define UART_BROKER_DEFINE(inst) ON_UART_BUS(inst, INST_DEFINE)

DT_INST_FOREACH_STATUS_OKAY(UART_BROKER_DEFINE)

static int init(struct pouch_uart_broker *ctx)
{
    ctx->broker = pouch_serial_broker_create(&ctx->adapter);
    if (ctx->broker == NULL)
    {
        LOG_ERR("Failed to create broker %p", ctx);
        return -ENOMEM;
    }

    int err = pouch_uart_init(&ctx->uart);
    if (err)
    {
        LOG_ERR("UART init failed: %d", err);
        return err;
    }

    k_work_init_delayable(&ctx->watchdog, watchdog_timeout);

    return 0;
}

#define INSTANCE_INIT(inst)                  \
    do                                       \
    {                                        \
        int err = init(&uart_broker_##inst); \
        if (err)                             \
        {                                    \
            return err;                      \
        }                                    \
    } while (0)

#define UART_BROKER_INIT(inst) ON_UART_BUS(inst, INSTANCE_INIT)

static int uart_broker_init(void)
{
    DT_INST_FOREACH_STATUS_OKAY(UART_BROKER_INIT);
    return 0;
}

SYS_INIT(uart_broker_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

#define START(inst)                                                \
    do                                                             \
    {                                                              \
        struct pouch_uart_broker *transport = &uart_broker_##inst; \
        pouch_serial_broker_start(transport->broker);              \
    } while (0)

#define UART_BROKER_START(inst) ON_UART_BUS(inst, START)

void pouch_uart_broker_start(void)
{
    DT_INST_FOREACH_STATUS_OKAY(UART_BROKER_START);
}
