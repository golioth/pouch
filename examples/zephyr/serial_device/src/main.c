/*
 * Copyright (c) 2025 Golioth
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main);

#include "credentials.h"

#include <zephyr/drivers/gpio.h>

#include <pouch/pouch.h>
#include <pouch/events.h>
#include <pouch/uplink.h>
#include <pouch/downlink.h>
#include <pouch/transport/serial/device.h>

#include <pouch/golioth/settings_callbacks.h>

#include <app_version.h>

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led0), gpios, {});

static void do_sync(struct k_work *work)
{
    LOG_INF("SYNC");
    pouch_serial_device_sync();
    k_work_schedule(k_work_delayable_from_work(work), K_SECONDS(CONFIG_EXAMPLE_SYNC_PERIOD_S));
}

static K_WORK_DELAYABLE_DEFINE(sync, do_sync);

/**
 * Push timeseries application data to the cloud on every uplink
 */
static void do_uplink(void)
{
    LOG_INF("Sending uplink data\n");
    const char *data = "{\"temp\":22}";
    pouch_uplink_entry_write(".s/sensor",
                             POUCH_CONTENT_TYPE_JSON,
                             data,
                             strlen(data),
                             POUCH_FOREVER);
}

POUCH_UPLINK_HANDLER(do_uplink);

/**
 * Settings handler for the "LED" setting.
 *
 * The settings handler gets called when the Settings service
 * receives a new value for the registered setting.
 */
static int led_setting_cb(bool new_value)
{
    LOG_INF("Received LED setting: %d", (int) new_value);

    if (DT_HAS_ALIAS(led0))
    {
        gpio_pin_set_dt(&led, new_value ? 1 : 0);
    }

    return 0;
}

GOLIOTH_SETTINGS_HANDLER(LED, led_setting_cb);

/**
 * Setup pouch stack.
 *
 * Loads credentials and initializes pouch.
 */
static int setup_pouch(void)
{
    struct pouch_config config = {0};

    int err = load_certificate(&config.certificate);
    if (err)
    {
        LOG_ERR("Failed to load certificate (err %d)", err);
        return err;
    }

    config.private_key = load_private_key();
    if (config.private_key == PSA_KEY_ID_NULL)
    {
        LOG_ERR("Failed to load private key");
        return -ENOENT;
    }

    LOG_INF("Credentials loaded");

    err = pouch_init(&config);
    if (err)
    {
        LOG_ERR("Pouch init failed (err %d)", err);
        return err;
    }

    LOG_INF("Pouch initialized");
    return 0;
}

/**
 * Initialize LED.
 *
 * If the LED can't be initialized, we'll log it, but not crash out.
 * The example can still work without it.
 */
static void setup_led(void)
{
    if (!DT_HAS_ALIAS(led0))
    {
        return;
    }

    int err = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
    if (err < 0)
    {
        LOG_WRN("Could not initialize LED");
    }
}

int main(void)
{
    LOG_INF("Pouch SDK Version: " STRINGIFY(APP_BUILD_VERSION));
    LOG_INF("Pouch Protocol Version: %d", POUCH_VERSION);

    int err = setup_pouch();
    if (err)
    {
        return err;
    }

    setup_led();

    k_work_schedule(&sync, K_NO_WAIT);

    return 0;
}
