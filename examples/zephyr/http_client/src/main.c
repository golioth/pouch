/*
 * Copyright (c) 2025 Golioth
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(http_transport, CONFIG_EXAMPLE_HTTP_CLIENT_LOG_LEVEL);

#include "credentials.h"
#include "net.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/http/client.h>
#include <zephyr/net/tls_credentials.h>

#include <pouch/pouch.h>
#include <pouch/events.h>
#include <pouch/uplink.h>
#include <pouch/downlink.h>
#include <pouch/transport/certificate.h>
#include <pouch/transport/http/client.h>

#include <golioth/settings_callbacks.h>

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led0), gpios, {});

static void do_uplink(void)
{
    const char *payload = "{\"temp\":22}";
    pouch_uplink_entry_write(".s/sensor",
                             POUCH_CONTENT_TYPE_JSON,
                             payload,
                             strlen(payload),
                             POUCH_FOREVER);
}
POUCH_UPLINK_HANDLER(do_uplink);

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
    struct pouch_config config = {0};

    /* Load certificates for Pouch */
    int err = load_certificate(&config.certificate);
    if (err)
    {
        LOG_ERR("Failed to load certificate (err %d)", err);
        return 0;
    }

    config.private_key = load_private_key();
    if (config.private_key == PSA_KEY_ID_NULL)
    {
        LOG_ERR("Failed to load private key");
        return 0;
    }

    LOG_INF("Credentials loaded");

    err = pouch_init(&config);
    if (err)
    {
        LOG_ERR("Pouch init failed (err %d)", err);
        return 0;
    }

    /* Load certificates for HTTP mTLS transport */
    err = load_http_server_ca(CONFIG_EXAMPLE_HTTP_CLIENT_TLS_CREDENTIALS);
    if (err)
    {
        LOG_ERR("Failed to load server CA certificate (err %d)", err);
        return 0;
    }

    err = load_http_gw_device_crt(CONFIG_EXAMPLE_HTTP_CLIENT_TLS_CREDENTIALS);
    if (err)
    {
        LOG_ERR("Failed to load device certificate (err %d)", err);
        return 0;
    }

    err = load_http_gw_device_key(CONFIG_EXAMPLE_HTTP_CLIENT_TLS_CREDENTIALS);
    if (err)
    {
        LOG_ERR("Failed to load device key (err %d)", err);
        return 0;
    }

    setup_led();

    net_connect();

    err = pouch_http_client_init(CONFIG_EXAMPLE_HTTP_CLIENT_TLS_CREDENTIALS, K_FOREVER);
    if (0 != err)
    {
        LOG_ERR("Failed to initialize HTTP client transport");
        return err;
    }

    while (1)
    {
        /* Sync Pouch uplink and downlink */
        pouch_http_client_sync(K_NO_WAIT);

        k_sleep(K_SECONDS(CONFIG_EXAMPLE_HTTP_CLIENT_SYNC_PERIOD_S));
    }

    return err;
}
