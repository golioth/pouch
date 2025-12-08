/*
 * Copyright (c) 2025 Golioth
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main);

#include "credentials.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/net/tls_credentials.h>

#include <pouch/pouch.h>
#include <pouch/events.h>
#include <pouch/uplink.h>
#include <pouch/downlink.h>

#include <pouch/transport/coap/pouch_coap.h>

#include <golioth/golioth.h>
#include <golioth/settings_callbacks.h>

#include <app_version.h>

#define CONFIG_POUCH_COAP_TLS_CREDENTIALS_TAG 515765868

static const uint8_t tls_ca_crt[] = {
#include "pouch-coap-ca-cert.inc"
};

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led0), gpios, {});

static void pouch_event_handler(enum pouch_event event, void *ctx)
{
    if (POUCH_EVENT_SESSION_START == event)
    {
        pouch_uplink_entry_write(".s/sensor",
                                 POUCH_CONTENT_TYPE_JSON,
                                 "{\"temp\":22}",
                                 sizeof("{\"temp\":22}") - 1,
                                 K_FOREVER);

        golioth_sync_to_cloud();
    }

    if (POUCH_EVENT_SESSION_END == event)
    {
        // k_work_schedule(&sync_request_work, K_SECONDS(CONFIG_EXAMPLE_SYNC_PERIOD_S));
    }
}

POUCH_EVENT_HANDLER(pouch_event_handler, NULL);

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

int main(void)
{
    LOG_INF("Pouch SDK Version: " STRINGIFY(APP_BUILD_VERSION));
    LOG_INF("Pouch Protocol Version: %d", POUCH_VERSION);

    struct pouch_config config = {0};

    int err = load_certificate(&config.certificate);
    if (err)
    {
        LOG_ERR("Failed to load certificate (err %d)", err);
        return 0;
    }

    err = tls_credential_add(CONFIG_POUCH_COAP_TLS_CREDENTIALS_TAG,
                             TLS_CREDENTIAL_PUBLIC_CERTIFICATE,
                             config.certificate.buffer,
                             config.certificate.size);
    if (err)
    {
        LOG_ERR("Failed to load certificate into TLS storage: %d", err);
        return 0;
    }

    config.private_key = load_private_key(CONFIG_POUCH_COAP_TLS_CREDENTIALS_TAG);
    if (config.private_key == PSA_KEY_ID_NULL)
    {
        LOG_ERR("Failed to load private key");
        return 0;
    }

    err = tls_credential_add(CONFIG_POUCH_COAP_TLS_CREDENTIALS_TAG,
                             TLS_CREDENTIAL_CA_CERTIFICATE,
                             tls_ca_crt,
                             sizeof(tls_ca_crt));
    if (err)
    {
        LOG_ERR("Failed to load CA cert");
    }

    LOG_INF("Credentials loaded");

    err = pouch_init(&config);
    if (err)
    {
        LOG_ERR("Pouch init failed (err %d)", err);
        return 0;
    }

    LOG_INF("Pouch initialized");

    if (DT_HAS_ALIAS(led0))
    {
        err = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
        if (err < 0)
        {
            LOG_ERR("Could not initialize LED");
        }
    }

    sec_tag_t sec_tag = CONFIG_POUCH_COAP_TLS_CREDENTIALS_TAG;
    pouch_coap_init(&sec_tag, 1);

    while (1)
    {
        pouch_coap_sync();
        k_sleep(K_SECONDS(CONFIG_EXAMPLE_SYNC_PERIOD_S));
    }
    return 0;
}
