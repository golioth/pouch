/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <pouch/transport/serial/broker.h>

#include <stdlib.h>
#include <string.h>

LOG_MODULE_REGISTER(pouch_spi_broker, CONFIG_POUCH_SPI_BROKER_LOG_LEVEL);

#define DT_DRV_COMPAT golioth_pouch_broker

struct spi_broker_ctx
{
    struct spi_dt_spec spi;
    struct pouch_serial_broker *broker;
    struct pouch_serial_broker_adapter adapter;
    struct gpio_dt_spec data_ready_gpio;
    struct gpio_callback gpio_cb;
    struct k_work_delayable work;
    struct k_work_delayable start_work;
    struct k_work_delayable retry_work;
    uint32_t poll_interval_ms;
    uint32_t retry_delay_ms;
    bool pending;
    bool sending;
    bool progress;  // The device has sent a valid frame since the retry timer was last armed
    bool has_data_ready_pin;
    uint8_t tx_buf[CONFIG_POUCH_SPI_BROKER_FRAME_SIZE];
    uint8_t rx_buf[CONFIG_POUCH_SPI_BROKER_FRAME_SIZE];
    size_t tx_len;  // Length of the frame in tx_buf, kept until it has been clocked out
    size_t rx_bytes;
};

static K_THREAD_STACK_DEFINE(work_q_stack, CONFIG_POUCH_SPI_BROKER_THREAD_STACK_SIZE);
static struct k_work_q spi_broker_work_q;

static void schedule_exchange(struct spi_broker_ctx *ctx)
{
    // Give the slave a bit of time to set its buffers. Reschedule, so a pending poll doesn't
    // hold back the exchange.
    k_work_reschedule_for_queue(&spi_broker_work_q,
                                &ctx->work,
                                K_MSEC(CONFIG_POUCH_SPI_BROKER_INTERFRAME_DELAY));
}

static void schedule_poll(struct spi_broker_ctx *ctx)
{
    if (!ctx->has_data_ready_pin)
    {
        LOG_DBG("schedule poll");
        pouch_serial_broker_notify(ctx->broker);
        k_work_schedule_for_queue(&spi_broker_work_q, &ctx->work, K_MSEC(ctx->poll_interval_ms));
    }
}

/*
 * Arm the retry timer while the broker waits for the device to respond, and restart it whenever
 * the device makes progress.
 */
static void update_retry_timer(struct spi_broker_ctx *ctx)
{
    if (!pouch_serial_broker_is_waiting(ctx->broker))
    {
        k_work_cancel_delayable(&ctx->retry_work);
    }
    else if (ctx->progress)
    {
        k_work_reschedule_for_queue(&spi_broker_work_q,
                                    &ctx->retry_work,
                                    K_MSEC(CONFIG_POUCH_SPI_BROKER_RETRY_TIMEOUT_MS));
    }
    else
    {
        // Keeps the deadline if the timer is already running
        k_work_schedule_for_queue(&spi_broker_work_q,
                                  &ctx->retry_work,
                                  K_MSEC(CONFIG_POUCH_SPI_BROKER_RETRY_TIMEOUT_MS));
    }

    ctx->progress = false;
}

static void adapter_ready(const struct pouch_serial_broker *broker)
{
    const struct pouch_serial_broker_adapter *adapter = pouch_serial_broker_adapter_get(broker);
    struct spi_broker_ctx *ctx = CONTAINER_OF(adapter, struct spi_broker_ctx, adapter);
    LOG_DBG("%p", broker);

    ctx->pending = true;
    if (!ctx->sending)
    {
        schedule_exchange(ctx);
    }
}

static void adapter_end(const struct pouch_serial_broker *broker, bool success)
{
    const struct pouch_serial_broker_adapter *adapter = pouch_serial_broker_adapter_get(broker);
    struct spi_broker_ctx *ctx = CONTAINER_OF(adapter, struct spi_broker_ctx, adapter);

    if (success)
    {
        LOG_DBG("Exchange completed");
        ctx->retry_delay_ms = CONFIG_POUCH_SPI_BROKER_RETRY_DELAY_MIN_MS;
        return;
    }

    // The broker stays idle after a failure until it's restarted. Back off, so a persistent
    // failure doesn't keep the bus and the cloud connection busy.
    LOG_WRN("Exchange failed, restarting in %u ms", ctx->retry_delay_ms);
    k_work_reschedule_for_queue(&spi_broker_work_q, &ctx->start_work, K_MSEC(ctx->retry_delay_ms));
    ctx->retry_delay_ms = MIN(2 * ctx->retry_delay_ms, CONFIG_POUCH_SPI_BROKER_RETRY_DELAY_MAX_MS);
}

static void data_ready_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    struct spi_broker_ctx *ctx = CONTAINER_OF(cb, struct spi_broker_ctx, gpio_cb);

    pouch_serial_broker_notify(ctx->broker);
    schedule_exchange(ctx);
}

static void prepare_rx(struct spi_broker_ctx *ctx, struct spi_buf *buf)
{
    // clear buffer to
    memset(&ctx->rx_buf[ctx->rx_bytes], 0, sizeof(ctx->rx_buf) - ctx->rx_bytes);

    if (ctx->rx_bytes == 0 || ctx->rx_buf[0] == 0)
    {
        buf->buf = &ctx->rx_buf[0];
        buf->len = 1;  // request length field
        return;
    }

    uint8_t len = ctx->rx_buf[0];
    if (ctx->rx_bytes > 1 + len)
    {
        LOG_ERR("Invalid pending frame length %u, discarding buffer", len);
        ctx->rx_bytes = 0;
        buf->buf = &ctx->rx_buf[0];
        buf->len = 1;  // request length field
        return;
    }

    if (len > sizeof(ctx->rx_buf) - 1)
    {
        LOG_ERR("Invalid pending frame length %u, discarding buffer", len);
        ctx->rx_bytes = 0;
        buf->buf = &ctx->rx_buf[0];
        buf->len = 1;  // request length field
        return;
    }

    buf->buf = &ctx->rx_buf[ctx->rx_bytes];
    buf->len = 1 + len - ctx->rx_bytes;
}

static void prepare_tx(struct spi_broker_ctx *ctx, struct spi_buf *buf)
{
    buf->buf = ctx->tx_buf;

    // A frame left over from a failed transfer is sent again before fetching a new one
    if (ctx->tx_len == 0)
    {
        size_t len =
            pouch_serial_broker_frame_get(ctx->broker, &ctx->tx_buf[1], sizeof(ctx->tx_buf) - 1);
        if (len == 0)
        {
            buf->len = 0;
            return;
        }

        ctx->tx_buf[0] = (uint8_t) len;
        ctx->tx_len = 1 + len;  // include length byte
    }

    buf->len = ctx->tx_len;
    ctx->pending = true;  // always check for a response from the slave after sending
}

static void trx(struct spi_broker_ctx *ctx)
{
    struct spi_buf tx_spi_buf;
    struct spi_buf rx_spi_buf;

    prepare_tx(ctx, &tx_spi_buf);
    prepare_rx(ctx, &rx_spi_buf);
    if (rx_spi_buf.len > tx_spi_buf.len)
    {
        memset(&ctx->tx_buf[tx_spi_buf.len], 0, rx_spi_buf.len - tx_spi_buf.len);
        tx_spi_buf.len = rx_spi_buf.len;
    }
    else if (tx_spi_buf.len > rx_spi_buf.len)
    {
        rx_spi_buf.len = MIN(tx_spi_buf.len, sizeof(ctx->rx_buf) - ctx->rx_bytes);
        memset(rx_spi_buf.buf, 0, rx_spi_buf.len);
    }

    const struct spi_buf_set tx_set = {
        .buffers = &tx_spi_buf,
        .count = 1,
    };
    const struct spi_buf_set rx_set = {
        .buffers = &rx_spi_buf,
        .count = 1,
    };

    LOG_HEXDUMP_DBG(tx_spi_buf.buf, tx_spi_buf.len, "TX");

    int err = spi_transceive_dt(&ctx->spi, &tx_set, &rx_set);

    if (err)
    {
        LOG_ERR("SPI transceive failed: %d", err);
        ctx->pending = true;  // retry the frame in tx_buf
        return;
    }

    ctx->tx_len = 0;

    // in master mode, spi_transceive_dt returns 0, so we have to keep track of the number of bytes
    // received ourselves
    ctx->rx_bytes += MIN(rx_spi_buf.len, tx_spi_buf.len);

    LOG_HEXDUMP_DBG(rx_spi_buf.buf, MIN(rx_spi_buf.len, tx_spi_buf.len), "RX");
}

static void process_rx(struct spi_broker_ctx *ctx, bool *has_more)
{
    /* Parse every complete frame the slave packed into the response.
     * Multiple frames may be present, so loop until no more complete
     * frames can be consumed.
     */
    size_t i = 0;
    while (i < ctx->rx_bytes)
    {
        uint8_t len = ctx->rx_buf[i];
        if (i + len >= ctx->rx_bytes)
        {
            // Incomplete frame. Shift this data to the start of the buffer and wait for more
            memmove(&ctx->rx_buf[0], &ctx->rx_buf[i], ctx->rx_bytes - i);
            ctx->rx_bytes -= i;
            *has_more = true;
            return;
        }

        const uint8_t *frame = &ctx->rx_buf[++i];
        if (len == 0)
        {
            /* Dummy byte. Slave has no data at this position. */
            continue;
        }

        int err = pouch_serial_broker_recv(ctx->broker, frame, len);
        if (err != 0)
        {
            LOG_ERR("Failed to deliver frame: %d", err);
        }
        else
        {
            ctx->progress = true;
        }

        // The frame may need a response, such as an ACK, and the slave might have more queued.
        ctx->pending = true;
        i += len;
    }

    ctx->rx_bytes = 0;
    *has_more = false;
}

static void exchange_work_handler(struct k_work *work)
{
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct spi_broker_ctx *ctx = CONTAINER_OF(dwork, struct spi_broker_ctx, work);
    bool has_more;
    ctx->sending = true;

    trx(ctx);

    process_rx(ctx, &has_more);
    if (has_more || ctx->pending)
    {
        // The slave has more data, so schedule another exchange with a slight delay to give it time
        // to prepare the next frame
        LOG_DBG("Scheduling followup");
        k_work_reschedule_for_queue(&spi_broker_work_q,
                                    &ctx->work,
                                    K_MSEC(CONFIG_POUCH_SPI_BROKER_INTERFRAME_DELAY));
        ctx->pending = false;
    }
    else
    {
        schedule_poll(ctx);
    }

    update_retry_timer(ctx);
    ctx->sending = false;
}

static void start_work_handler(struct k_work *work)
{
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct spi_broker_ctx *ctx = CONTAINER_OF(dwork, struct spi_broker_ctx, start_work);

    pouch_serial_broker_start(ctx->broker);
    schedule_poll(ctx);
}

static void retry_work_handler(struct k_work *work)
{
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct spi_broker_ctx *ctx = CONTAINER_OF(dwork, struct spi_broker_ctx, retry_work);

    // The broker queues the retries, and signals the adapter to send them
    pouch_serial_broker_timeout(ctx->broker);
}

// Wrapper ensuring that the fn only gets invoked if the instance is on a SPI bus
#define ON_SPI_BUS(inst, fn) IF_ENABLED(DT_INST_ON_BUS(inst, spi), (fn))

#define SPI_BROKER_HAS_DATA_READY(inst) DT_INST_NODE_HAS_PROP(inst, data_ready_gpios)

#define SPI_BROKER_DATA_READY_GPIO(inst)        \
    IF_ENABLED(SPI_BROKER_HAS_DATA_READY(inst), \
               (.data_ready_gpio = GPIO_DT_SPEC_INST_GET(inst, data_ready_gpios), ))

#define DEFINE_INSTANCE(inst)                                                                  \
    static struct spi_broker_ctx spi_broker_ctx_##inst = {                                     \
        .spi = SPI_DT_SPEC_INST_GET(inst, SPI_WORD_SET(8) | SPI_TRANSFER_MSB),                 \
        .adapter =                                                                             \
            {                                                                                  \
                .ready = adapter_ready,                                                        \
                .end = adapter_end,                                                            \
            },                                                                                 \
        SPI_BROKER_DATA_READY_GPIO(inst).has_data_ready_pin = SPI_BROKER_HAS_DATA_READY(inst), \
        .poll_interval_ms = DT_INST_PROP_OR(inst, poll_interval_ms, 0),                        \
        .retry_delay_ms = CONFIG_POUCH_SPI_BROKER_RETRY_DELAY_MIN_MS,                          \
    };

#define SPI_BROKER_DEFINE(inst) ON_SPI_BUS(inst, DEFINE_INSTANCE(inst));

DT_INST_FOREACH_STATUS_OKAY(SPI_BROKER_DEFINE)

static int data_ready_init(struct spi_broker_ctx *ctx)
{
    if (!gpio_is_ready_dt(&ctx->data_ready_gpio))
    {
        LOG_ERR("Data-ready GPIO not ready (%p)", ctx);
        return -ENODEV;
    }

    int err = gpio_pin_configure_dt(&ctx->data_ready_gpio, GPIO_INPUT);
    if (err != 0)
    {
        LOG_ERR("Failed to configure data-ready GPIO (%p): %d", ctx, err);
        return err;
    }

    gpio_init_callback(&ctx->gpio_cb, data_ready_isr, BIT(ctx->data_ready_gpio.pin));
    err = gpio_add_callback(ctx->data_ready_gpio.port, &ctx->gpio_cb);
    if (err != 0)
    {
        LOG_ERR("Failed to add data-ready callback (%p): %d", ctx, err);
        return err;
    }

    err = gpio_pin_interrupt_configure_dt(&ctx->data_ready_gpio, GPIO_INT_EDGE_TO_ACTIVE);
    if (err != 0)
    {
        LOG_ERR("Failed to configure data-ready interrupt (%p): %d", ctx, err);
        gpio_remove_callback(ctx->data_ready_gpio.port, &ctx->gpio_cb);
        return err;
    }

    return 0;
}

static int instance_init(struct spi_broker_ctx *ctx)
{
    if (!spi_is_ready_dt(&ctx->spi))
    {
        LOG_ERR("SPI device not ready (%p)", ctx);
        return -ENODEV;
    }

    k_work_init_delayable(&ctx->work, exchange_work_handler);
    k_work_init_delayable(&ctx->start_work, start_work_handler);
    k_work_init_delayable(&ctx->retry_work, retry_work_handler);

    ctx->broker = pouch_serial_broker_create(&ctx->adapter);
    if (ctx->broker == NULL)
    {
        LOG_ERR("Failed to create broker (%p)", ctx);
        return -ENOMEM;
    }

    // Arm the data-ready interrupt last, as it may fire right away
    if (ctx->has_data_ready_pin)
    {
        int err = data_ready_init(ctx);
        if (err)
        {
            free(ctx->broker);
            ctx->broker = NULL;
            return err;
        }
    }

    LOG_DBG("Initialized SPI broker (%p)", ctx);
    return 0;
}

#define SPI_BROKER_INIT_INSTANCE(inst) ON_SPI_BUS(inst, instance_init(&spi_broker_ctx_##inst));

static int spi_broker_init(void)
{
    k_work_queue_init(&spi_broker_work_q);
    k_work_queue_start(&spi_broker_work_q,
                       work_q_stack,
                       K_THREAD_STACK_SIZEOF(work_q_stack),
                       K_PRIO_COOP(7),
                       NULL);
    k_thread_name_set(&spi_broker_work_q.thread, "pouch_spi_brk");

    DT_INST_FOREACH_STATUS_OKAY(SPI_BROKER_INIT_INSTANCE);

    return 0;
}

SYS_INIT(spi_broker_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

static void broker_start(struct spi_broker_ctx *ctx)
{
    if (ctx->broker == NULL)
    {
        // Initialization failed, and has already been logged
        return;
    }

    // Drive the broker from the work queue only, so it's never accessed from two threads at once
    k_work_reschedule_for_queue(&spi_broker_work_q, &ctx->start_work, K_NO_WAIT);
}

#define SPI_BROKER_START(inst) ON_SPI_BUS(inst, broker_start(&spi_broker_ctx_##inst));

void pouch_spi_broker_start(void)
{
    DT_INST_FOREACH_STATUS_OKAY(SPI_BROKER_START);
}
