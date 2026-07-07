/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/net/net_if.h>
#include <zephyr/net/dhcpv4.h>

#include <golioth/client.h>
#include <golioth/gateway.h>
#include <samples/common/sample_credentials.h>

#include <pouch/gateway/cert.h>
#include <pouch/gateway/downlink.h>
#include <pouch/gateway/uplink.h>
#include <pouch/transport/serial/broker.h>
#include <pouch/transport/spi/broker.h>
#include <pouch/transport/uart/broker.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

static struct golioth_client *client;
static K_SEM_DEFINE(connected, 0, 1);

static void on_client_event(struct golioth_client *client,
                            enum golioth_client_event event,
                            void *arg)
{
    bool is_connected = (event == GOLIOTH_CLIENT_EVENT_CONNECTED);

    if (is_connected)
    {
        k_sem_give(&connected);
    }

    printf("Golioth client %s\n", is_connected ? "connected" : "disconnected");
}

static void connect_golioth_client(void)
{
    const struct golioth_client_config *client_config = golioth_sample_credentials_get();
    if (client_config == NULL || client_config->credentials.psk.psk_id_len == 0
        || client_config->credentials.psk.psk_len == 0)
    {
        LOG_ERR("No credentials found.");
        LOG_ERR(
            "Please store your credentials with the following commands, then reboot the device.");
        LOG_ERR("\tsettings set golioth/psk-id <your-psk-id>");
        LOG_ERR("\tsettings set golioth/psk <your-psk>");
        return;
    }


    client = golioth_client_create(client_config);

    golioth_client_register_event_callback(client, on_client_event, NULL);
}

struct net_wait_data
{
    struct k_sem sem;
    struct net_mgmt_event_callback cb;
};

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

static void connect_to_cloud(void)
{
    struct net_if *iface = net_if_get_default();

    if (!net_if_is_up(iface))
    {
        printf("Bringing up network interface (%p)\n", (void *) iface);
        int err = net_if_up(iface);

        if ((err < 0) && (err != -EALREADY))
        {
            printf("Failed to bring up network interface: %d\n", err);
            return;
        }
    }

    if (IS_ENABLED(CONFIG_NET_L2_ETHERNET) && IS_ENABLED(CONFIG_NET_DHCPV4))
    {
        net_dhcpv4_start(net_if_get_default());
    }
    else if (IS_ENABLED(CONFIG_MODEM))
    {
        printf("Waiting to obtain IP address\n");
        wait_for_net_event(iface,
                           IS_ENABLED(DNS_SERVER_IP_ADDRESSES) ? NET_EVENT_DNS_SERVER_ADD
                                                               : NET_EVENT_IPV4_ADDR_ADD);
    }

    connect_golioth_client();
    k_sem_take(&connected, K_FOREVER);
}

int main(void)
{
    printf("Serial Gateway Example\n");

    connect_to_cloud();

    pouch_gateway_cert_module_on_connected(client);
    pouch_gateway_uplink_module_init(client);
    pouch_gateway_downlink_module_init(client);

    if (IS_ENABLED(CONFIG_POUCH_SPI_BROKER))
    {
        pouch_spi_broker_start();
    }
    if (IS_ENABLED(CONFIG_POUCH_UART_BROKER))
    {
        pouch_uart_broker_start();
    }

    return 0;
}
