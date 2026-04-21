/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(coap_transport, CONFIG_EXAMPLE_COAP_CLIENT_LOG_LEVEL);

#include "credentials.h"

#ifdef CONFIG_WIFI
#include "wifi.h"
#endif

#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>

#include <pouch/pouch.h>
#include <pouch/uplink.h>
#include <pouch/transport/certificate.h>
#include <pouch/transport/coap/client.h>

#include <pouch/golioth/settings_callbacks.h>

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

    return 0;
}

GOLIOTH_SETTINGS_HANDLER(LED, led_setting_cb);

int main(void)
{
    struct pouch_config config = {0};

    /* Load certificates for Pouch */
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
        return -EIO;
    }

    LOG_INF("Credentials loaded");

    err = pouch_init(&config);
    if (err)
    {
        LOG_ERR("Pouch init failed (err %d)", err);
        return err;
    }

    /* Load certificates for DTLS transport */
    err = load_coap_server_ca(CONFIG_EXAMPLE_COAP_CLIENT_DTLS_CREDENTIALS);
    if (err)
    {
        LOG_ERR("Failed to load server CA certificate (err %d)", err);
        return err;
    }

    err = load_coap_gw_device_crt(CONFIG_EXAMPLE_COAP_CLIENT_DTLS_CREDENTIALS);
    if (err)
    {
        LOG_ERR("Failed to load device certificate (err %d)", err);
        return err;
    }

    err = load_coap_gw_device_key(CONFIG_EXAMPLE_COAP_CLIENT_DTLS_CREDENTIALS);
    if (err)
    {
        LOG_ERR("Failed to load device key (err %d)", err);
        return err;
    }

    IF_ENABLED(CONFIG_WIFI, (wifi_connect()));

    err = pouch_coap_client_init(CONFIG_EXAMPLE_COAP_CLIENT_DTLS_CREDENTIALS);
    if (err)
    {
        LOG_ERR("Failed to initialize CoAP client transport");
        return err;
    }

    while (1)
    {
        /* Sync Pouch uplink and downlink */
        err = pouch_coap_client_sync();
        if (err)
        {
            LOG_WRN("Pouch sync failed (err %d)", err);
        }

        k_sleep(K_SECONDS(CONFIG_EXAMPLE_COAP_CLIENT_SYNC_PERIOD_S));
    }

    return 0;
}
