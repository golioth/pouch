/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(example_wifi);

#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/wifi_credentials.h>
#include <zephyr/settings/settings.h>

static K_SEM_DEFINE(wifi_connected, 0, 1);
static struct net_mgmt_event_callback wifi_cb;

static void connect_cb(void *cb_arg, const char *ssid, size_t ssid_len)
{
    struct net_if *iface = cb_arg;
    struct wifi_credentials_personal creds = {0};

    if (wifi_credentials_get_by_ssid_personal_struct(ssid, ssid_len, &creds) == 0)
    {
        struct wifi_connect_req_params params = {
            .ssid = creds.header.ssid,
            .ssid_length = creds.header.ssid_len,
            .psk = creds.password,
            .psk_length = creds.password_len,
            .security = creds.header.type,
        };
        net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &params, sizeof(params));
    }
}

static void wifi_mgmt_event_handler(struct net_mgmt_event_callback *cb,
                                    uint64_t mgmt_event,
                                    struct net_if *iface)
{
    if (mgmt_event == NET_EVENT_WIFI_CONNECT_RESULT)
    {
        const struct wifi_status *status = (const struct wifi_status *) cb->info;

        if (status->status)
        {
            LOG_ERR("WiFi connection failed (status %d)", status->status);
        }
        else
        {
            LOG_INF("WiFi connected");
            k_sem_give(&wifi_connected);
        }
    }
}

void wifi_connect(void)
{
    net_mgmt_init_event_callback(&wifi_cb, wifi_mgmt_event_handler, NET_EVENT_WIFI_CONNECT_RESULT);
    net_mgmt_add_event_callback(&wifi_cb);

    LOG_INF("Initializing settings");
    settings_subsys_init();
    settings_load();

    LOG_INF("Connecting to WiFi");
    struct net_if *iface = net_if_get_wifi_sta();
    if (wifi_credentials_is_empty())
    {
        LOG_ERR("No WiFi credentials available");
        LOG_ERR("   After using `wifi cred add ...` to set, run `kernel reboot`");
        k_sleep(K_FOREVER);
    }
    wifi_credentials_for_each_ssid(connect_cb, iface);

    k_sem_take(&wifi_connected, K_FOREVER);
}
