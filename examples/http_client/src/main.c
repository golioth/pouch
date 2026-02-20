/*
 * Copyright (c) 2019 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(http_transport, CONFIG_EXAMPLE_HTTP_CLIENT_LOG_LEVEL);

#include "credentials.h"
#include "conn_monitor.h"

#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/http/client.h>
#include <zephyr/net/tls_credentials.h>

#include <pouch/pouch.h>
#include <pouch/events.h>
#include <pouch/uplink.h>
#include <pouch/downlink.h>
#include <pouch/transport/certificate.h>
#include <pouch/transport/http/client.h>

#include <golioth/golioth.h>
#include <golioth/settings_callbacks.h>

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
}

POUCH_EVENT_HANDLER(pouch_event_handler, NULL);

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

    /* Wait for connection */
    wait_for_network();

    sec_tag_t sec_tag_list[] = {CONFIG_EXAMPLE_HTTP_CLIENT_TLS_CREDENTIALS};
    pouch_http_client_init(sec_tag_list, 1);

    while (1)
    {
        if (true == is_connected())
        {
            /* Sync Pouch uplink and downlink */
            pouch_http_client_sync();
        }
        else
        {
            LOG_INF("No network connection, Pouch sync skipped");
        }

        k_sleep(K_SECONDS(CONFIG_EXAMPLE_HTTP_CLIENT_SYNC_PERIOD_S));
    }

    return err;
}
