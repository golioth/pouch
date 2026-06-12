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
#include <zephyr/sys/spsc_pbuf.h>

#include <pouch/transport/serial/device.h>

#include <string.h>

LOG_MODULE_REGISTER(pouch_spi_device, CONFIG_POUCH_SPI_DEVICE_LOG_LEVEL);

#define DT_DRV_COMPAT golioth_pouch_device

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
             "Exactly one golioth,pouch-device-spi node is required");

#define HAS_DATA_READY_GPIO DT_INST_NODE_HAS_PROP(0, data_ready_gpios)

static struct spi_dt_spec spi =
    SPI_DT_SPEC_INST_GET(0, SPI_OP_MODE_SLAVE | SPI_WORD_SET(8) | SPI_TRANSFER_MSB);

static const struct gpio_dt_spec data_ready_gpio =
    GPIO_DT_SPEC_INST_GET_OR(0, data_ready_gpios, {0});

static uint8_t tx_buf[CONFIG_POUCH_SPI_DEVICE_FRAME_SIZE];
static uint8_t rx_buf[sizeof(struct spsc_pbuf) + CONFIG_POUCH_SPI_DEVICE_RX_BUFFER_SIZE] __aligned(
    4);
static struct k_work process_work;
static struct spsc_pbuf *rx_pbuf;
static atomic_t flags;

enum flags
{
    FLAG_BUSY,
    FLAG_TX_IN_PROGRESS,
    FLAG_SYNC_REQUESTED,
    FLAG_READY,
};

static struct spi_buf rx_spi_buf = {
    .buf = rx_buf,
    .len = sizeof(rx_buf),
};
static struct spi_buf tx_spi_buf = {
    .buf = tx_buf,
    .len = 0,
};

static int setup_transfer(void);

static void set_data_ready(bool ready)
{
    if (HAS_DATA_READY_GPIO)
    {
        gpio_pin_set_dt(&data_ready_gpio, ready);
    }
}

static void ready_cb(void)
{
    /* Set the data ready pin as soon as we have data to send.
     * As the SPI transfer is already in progress, we won't be able to swap in the pending
     * TX buffer before the master clocks out a transaction, but we'll set the pin high
     * again afterwards to prompt a second transfer once the pending buffer is in place.
     */
    atomic_set_bit(&flags, FLAG_READY);
    set_data_ready(true);
}

static void spi_xfer_complete(const struct device *dev, int result, void *data)
{
    set_data_ready(false);
    atomic_clear_bit(&flags, FLAG_READY);

    if (result < 0)
    {
        LOG_ERR("Transfer failed: %d", result);
        atomic_clear_bit(&flags, FLAG_BUSY);
        setup_transfer();
        return;
    }

    // the first byte is the length
    uint8_t len = ((uint8_t *) rx_spi_buf.buf)[0];
    if (result > 0 && len > 0)
    {
        spsc_pbuf_commit(rx_pbuf, MIN(result, 1 + len));
    }

    k_work_submit(&process_work);

    if (atomic_test_bit(&flags, FLAG_TX_IN_PROGRESS))
    {
        if (tx_spi_buf.len > result)
        {
            // There's more data to be sent
            tx_spi_buf.buf = ((uint8_t *) tx_spi_buf.buf) + result;
            tx_spi_buf.len -= result;
        }
        else
        {
            tx_spi_buf.buf = NULL;
            tx_spi_buf.len = 0;
            atomic_clear_bit(&flags, FLAG_TX_IN_PROGRESS);
        }
    }

    atomic_clear_bit(&flags, FLAG_BUSY);
    setup_transfer();
}

static void process_rx(void)
{
    while (true)
    {
        uint8_t *buf;
        int res = spsc_pbuf_claim(rx_pbuf, (char **) &buf);
        if (res < 1)
        {
            // received nothing
            return;
        }

        size_t len = buf[0];
        const uint8_t *data = &buf[1];
        if (res < 1 + len)
        {
            // not ready yet, will process again after next RX
            LOG_WRN("Waiting for %u bytes", len + 1 - res);
            return;
        }

        LOG_HEXDUMP_DBG(buf, res, "RX");

        int err = pouch_serial_device_recv(data, len);
        if (err)
        {
            LOG_ERR("RX process failed: %d", err);
        }

        spsc_pbuf_free(rx_pbuf, res);
    }
}

static void setup_tx(void)
{
    if (tx_spi_buf.buf != NULL)
    {
        // still processing previous TX
        return;
    }

    size_t len = pouch_serial_device_frame_get(&tx_buf[1], sizeof(tx_buf) - 1);
    if (!len)
    {
        return;
    }

    tx_buf[0] = (uint8_t) len;
    tx_spi_buf.len = 1 + len;
    tx_spi_buf.buf = tx_buf;

    if (atomic_test_bit(&flags, FLAG_BUSY))
    {
        // The bus is set up with no TX data, so we'll signal the master to do a poll just to flush
        // the NOOP byte out. We'll set up a new transfer directly after, which will include our TX
        // data.
        set_data_ready(true);
    }

    LOG_HEXDUMP_DBG(tx_spi_buf.buf, tx_spi_buf.len, "TX");
}

static void process(struct k_work *work)
{
    process_rx();
    setup_tx();
    setup_transfer();
}

static int setup_transfer(void)
{
    static const struct spi_buf_set tx_set = {
        .buffers = &tx_spi_buf,
        .count = 1,
    };
    static const struct spi_buf_set rx_set = {
        .buffers = &rx_spi_buf,
        .count = 1,
    };

    if (atomic_test_and_set_bit(&flags, FLAG_BUSY))
    {
        // We'll come back to setup the transfer as soon as the current one is done
        return -EBUSY;
    }

    char *buf;
    int res = spsc_pbuf_alloc(rx_pbuf, 1 + CONFIG_POUCH_SPI_DEVICE_FRAME_SIZE, &buf);
    if (res != 1 + CONFIG_POUCH_SPI_DEVICE_FRAME_SIZE)
    {
        // This can only happen if there's RX data waiting to be processed.
        // We'll come back to set up the next transfer after processing.
        LOG_DBG("RX buf alloc failed (%d)", res);
        atomic_clear_bit(&flags, FLAG_BUSY);
        return res;
    }

    rx_spi_buf.buf = buf;
    rx_spi_buf.len = 1 + CONFIG_POUCH_SPI_DEVICE_FRAME_SIZE;
    atomic_set_bit_to(&flags, FLAG_TX_IN_PROGRESS, tx_spi_buf.buf != NULL);

    res = spi_transceive_cb(spi.bus,
                            &spi.config,
                            tx_spi_buf.buf ? &tx_set : NULL,
                            &rx_set,
                            spi_xfer_complete,
                            NULL);
    if (res < 0)
    {
        LOG_ERR("Transceive failed (%d)", res);
        atomic_clear_bit(&flags, FLAG_BUSY);
        return res;
    }

    if (tx_spi_buf.len || atomic_test_bit(&flags, FLAG_READY))
    {
        set_data_ready(true);
    }

    return 0;
}

static int spi_slave_init(void)
{
    if (!spi_is_ready_dt(&spi))
    {
        LOG_ERR("SPI device not ready");
        return -ENODEV;
    }

    rx_pbuf = spsc_pbuf_init(rx_buf, sizeof(rx_buf), 0);
    if (rx_pbuf == NULL)
    {
        LOG_ERR("Failed to initialize RX buffer");
        return -ENOMEM;
    }

    k_work_init(&process_work, process);

    if (HAS_DATA_READY_GPIO)
    {
        if (!gpio_is_ready_dt(&data_ready_gpio))
        {
            LOG_ERR("Data-ready GPIO not ready");
            return -ENODEV;
        }

        int err = gpio_pin_configure_dt(&data_ready_gpio, GPIO_OUTPUT_INACTIVE);

        if (err != 0)
        {
            LOG_ERR("Failed to configure data-ready GPIO: %d", err);
            return err;
        }
    }

    pouch_serial_device_init(ready_cb);

    k_work_submit(&process_work);

    LOG_DBG("SPI device ready");

    return 0;
}

SYS_INIT(spi_slave_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
