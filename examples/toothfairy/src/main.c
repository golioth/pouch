/*
 * Copyright (c) 2025 Golioth
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>

#include <pouch/events.h>
#include <pouch/uplink.h>
#include <pouch/transport/toothfairy/peripheral.h>

#include <zephyr/drivers/gpio.h>

int sync_intervals[3] = {10, 5, 2};

int sync_idx = 0;

// TEMP SETUP

const struct device *const temp = DEVICE_DT_GET_ANY(silabs_si7055);

// BUTTON SETUP

/*
 * Get button configuration from the devicetree sw0 alias. This is mandatory.
 */
#define SW0_NODE DT_ALIAS(sw0)
#if !DT_NODE_HAS_STATUS_OKAY(SW0_NODE)
#error "Unsupported board: sw0 devicetree alias is not defined"
#endif
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET_OR(SW0_NODE, gpios, {0});

static struct gpio_callback button_cb_data;

void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    if (sync_idx < 2)
    {
        sync_idx++;
    }
    else
    {
        sync_idx = 0;
    };
    LOG_DBG("Sync interval set to %d", sync_intervals[sync_idx]);
}

// LED SETUP
/*
 * The led0 devicetree alias is optional. If present, we'll use it
 * to turn on the LED whenever the button is pressed.
 */
static struct gpio_dt_spec led = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led1), gpios, {0});

void led_work_handler(struct k_work *work)
{
    gpio_pin_set_dt(&led, 0);
}

K_WORK_DELAYABLE_DEFINE(led_work, led_work_handler);

// BT SETUP

static uint8_t service_data[] = {TF_UUID_GOLIOTH_SVC_VAL, 0x00};

static struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_SVC_DATA128, service_data, ARRAY_SIZE(service_data)),
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err)
    {
        LOG_DBG("Connection failed (err 0x%02x)", err);
    }
    else
    {
        LOG_DBG("Connected");
    }
    gpio_pin_set_dt(&led, 1);
}

void disconnect_work_handler(struct k_work *work)
{
    int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err)
    {
        LOG_ERR("Advertising failed to start (err %d)", err);
    }
}

K_WORK_DELAYABLE_DEFINE(disconnect_work, disconnect_work_handler);

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    LOG_DBG("Disconnected (reason 0x%02x)", reason);

    k_work_schedule(&led_work, K_SECONDS(1));
    k_work_schedule(&disconnect_work, K_SECONDS(1));
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

void sync_request_work_handler(struct k_work *work)
{
    service_data[ARRAY_SIZE(service_data) - 1] = 0x01;
    bt_le_adv_update_data(ad, ARRAY_SIZE(ad), NULL, 0);
}

K_WORK_DELAYABLE_DEFINE(sync_request_work, sync_request_work_handler);

static void pouch_event_handler(enum pouch_event event, void *ctx)
{
    if (POUCH_EVENT_SESSION_START == event)
    {
        int err = sensor_sample_fetch(temp);
        if (err)
        {
            LOG_ERR("sensor_sample_fetch failed ret %d\n", err);
        }

        struct sensor_value temp_value;
        err = sensor_channel_get(temp, SENSOR_CHAN_AMBIENT_TEMP, &temp_value);
        if (err)
        {
            LOG_ERR("sensor_channel_get failed ret %d\n", err);
        }

        char buf[256];
        snprintk(buf, sizeof(buf), "{\"temp\":%d.%02d}", temp_value.val1, abs(temp_value.val2));
        LOG_INF("%s", buf);
        pouch_uplink_entry_write(".s/sensor", POUCH_CONTENT_TYPE_JSON, buf, strlen(buf), K_FOREVER);
        pouch_uplink_close(K_FOREVER);
    }

    if (POUCH_EVENT_SESSION_END == event)
    {
        service_data[ARRAY_SIZE(service_data) - 1] = 0x00;
        k_work_schedule(&sync_request_work, K_SECONDS(sync_intervals[sync_idx]));
    }
}

POUCH_EVENT_HANDLER(pouch_event_handler, NULL);

int main(void)
{
    int err;

    // Button setup.
    if (!gpio_is_ready_dt(&button))
    {
        LOG_ERR("Error: button device %s is not ready\n", button.port->name);
        return 0;
    }

    err = gpio_pin_configure_dt(&button, GPIO_INPUT);
    if (err != 0)
    {
        LOG_ERR("Error %d: failed to configure %s pin %d\n", err, button.port->name, button.pin);
        return 0;
    }

    err = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
    if (err != 0)
    {
        LOG_ERR("Error %d: failed to configure interrupt on %s pin %d\n",
                err,
                button.port->name,
                button.pin);
        return 0;
    }

    gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
    gpio_add_callback(button.port, &button_cb_data);

    // LED setup.

    if (led.port && !gpio_is_ready_dt(&led))
    {
        LOG_ERR("Error %d: LED device %s is not ready; ignoring it\n", err, led.port->name);
        led.port = NULL;
    }
    if (led.port)
    {
        err = gpio_pin_configure_dt(&led, GPIO_OUTPUT);
        if (err != 0)
        {
            LOG_ERR("Error %d: failed to configure LED device %s pin %d\n",
                    err,
                    led.port->name,
                    led.pin);
            led.port = NULL;
        }
        else
        {
            LOG_DBG("Set up LED at %s pin %d\n", led.port->name, led.pin);
        }
    }

    // Temp setup.
    if (!device_is_ready(temp))
    {
        LOG_ERR("Error: temperature device is not ready\n");
        return 0;
    }

    // BT setup.
    struct toothfairy_peripheral *tf_peripheral =
        toothfairy_peripheral_create(CONFIG_POUCH_DEVICE_ID);
    if (NULL == tf_peripheral)
    {
        LOG_ERR("Failed to create toothfairy peripheral");
    }

    err = bt_enable(NULL);
    if (err)
    {
        LOG_ERR("Bluetooth init failed (err %d)", err);
        return 0;
    }

    LOG_DBG("Bluetooth initialized\n");

    err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err)
    {
        LOG_ERR("Advertising failed to start (err %d)", err);
        return 0;
    }

    LOG_DBG("Advertising successfully started");

    k_work_schedule(&sync_request_work, K_SECONDS(sync_intervals[sync_idx]));

    while (1)
    {
        k_sleep(K_SECONDS(1));
    }
    return 0;
}
