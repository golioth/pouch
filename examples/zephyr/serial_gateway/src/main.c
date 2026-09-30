/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/dhcpv4.h>

#include <pouch/pouch.h>
#include <pouch/uplink.h>
#include <pouch/transport/coap/client.h>
#include <pouch/transport/coap/gateway.h>
#include <pouch/transport/spi/broker.h>

#include <pouch/gateway/cert.h>
#include <pouch/gateway/cloud.h>
#include <pouch/gateway/uplink.h>

#include <golioth/settings_callbacks.h>

#include "credentials.h"

#if defined(CONFIG_WIFI)
#include "wifi.h"
#endif

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main);

struct net_wait_data
{
    struct k_sem sem;
    struct net_mgmt_event_callback cb;
};

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

static void event_cb_handler(struct net_mgmt_event_callback *cb,
                             uint64_t mgmt_event,
                             struct net_if *iface)
{
    struct net_wait_data *wait = CONTAINER_OF(cb, struct net_wait_data, cb);

    if (mgmt_event == cb->event_mask)
    {
        k_sem_give(&wait->sem);
    }
}

static void wait_for_net_event(struct net_if *iface, uint64_t event)
{
    struct net_wait_data wait;

    wait.cb.handler = event_cb_handler;
    wait.cb.event_mask = event;

    k_sem_init(&wait.sem, 0, 1);
    net_mgmt_add_event_callback(&wait.cb);

    k_sem_take(&wait.sem, K_FOREVER);

    net_mgmt_del_event_callback(&wait.cb);
}

static void wait_for_network(void)
{
    struct net_if *iface = net_if_get_default();

    if (!net_if_is_up(iface))
    {
        LOG_INF("Bringing up network interface (%p)", (void *) iface);
        int ret = net_if_up(iface);
        if ((ret < 0) && (ret != -EALREADY))
        {
            LOG_ERR("Failed to bring up network interface: %d", ret);
            return;
        }
    }

    if (IS_ENABLED(CONFIG_WIFI))
    {
        struct net_wait_data wait;

        wait.cb.handler = event_cb_handler;
        wait.cb.event_mask = NET_EVENT_IPV4_ADDR_ADD;
        k_sem_init(&wait.sem, 0, 1);
        net_mgmt_add_event_callback(&wait.cb);

        IF_ENABLED(CONFIG_WIFI, (wifi_connect()));

        if (IS_ENABLED(CONFIG_NET_DHCPV4))
        {
            net_dhcpv4_start(iface);
        }

        LOG_INF("Waiting to obtain IP address");
        k_sem_take(&wait.sem, K_FOREVER);
        net_mgmt_del_event_callback(&wait.cb);
    }
    else if (IS_ENABLED(CONFIG_NET_L2_ETHERNET) && IS_ENABLED(CONFIG_NET_DHCPV4))
    {
        net_dhcpv4_start(net_if_get_default());
    }
    else if (IS_ENABLED(CONFIG_MODEM))
    {
        LOG_INF("Waiting to obtain IP address");
        wait_for_net_event(iface,
                           IS_ENABLED(DNS_SERVER_IP_ADDRESSES) ? NET_EVENT_DNS_SERVER_ADD
                                                               : NET_EVENT_IPV4_ADDR_ADD);
    }
}

int main(void)
{
    int err;

    LOG_INF("Pouch SPI Serial Gateway");

    /* Load the Pouch device's identity (cert + key). */
    struct pouch_config config = {0};

    err = load_certificate(&config.certificate);
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

    /* Load DTLS credentials for the CoAP transport. */
    if (IS_ENABLED(CONFIG_POUCH_GATEWAY_CLOUD))
    {
        err = load_coap_server_ca(CONFIG_EXAMPLE_COAP_CLIENT_DTLS_CREDENTIALS);
        if (err)
        {
            LOG_ERR("Failed to load server CA certificate (err %d)", err);
            return 0;
        }

        err = load_coap_gw_device_crt(CONFIG_EXAMPLE_COAP_CLIENT_DTLS_CREDENTIALS);
        if (err)
        {
            LOG_ERR("Failed to load device certificate (err %d)", err);
            return 0;
        }

        err = load_coap_gw_device_key(CONFIG_EXAMPLE_COAP_CLIENT_DTLS_CREDENTIALS);
        if (err)
        {
            LOG_ERR("Failed to load device key (err %d)", err);
            return 0;
        }

        err = pouch_coap_client_init(CONFIG_EXAMPLE_COAP_CLIENT_DTLS_CREDENTIALS);
        if (err)
        {
            LOG_ERR("Failed to initialize CoAP client transport: %d", err);
            return err;
        }

        pouch_coap_gateway_init();
    }

    /* Wait for network and prime the cloud transport so the server
     * certificate is available before the SPI device is synced.
     */
    if (IS_ENABLED(CONFIG_POUCH_GATEWAY_CLOUD))
    {
        wait_for_network();

        while (true)
        {
            err = pouch_gateway_cloud_ensure_ready();
            if (!err)
            {
                break;
            }
            LOG_WRN("Initial cloud transport setup failed: %d, will retry", err);
            k_sleep(K_SECONDS(5));
        }
    }

    pouch_gateway_cert_module_init();
    pouch_gateway_uplink_module_init();

    pouch_spi_broker_start();

    if (IS_ENABLED(CONFIG_POUCH_GATEWAY_CLOUD))
    {
        while (true)
        {
            k_sleep(K_SECONDS(CONFIG_EXAMPLE_COAP_CLIENT_SYNC_PERIOD_S));

            err = pouch_coap_client_sync();
            if (err)
            {
                LOG_WRN("CoAP sync failed: %d", err);
            }
        }
    }

    return 0;
}
