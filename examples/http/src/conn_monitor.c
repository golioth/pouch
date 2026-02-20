/*
 * Copyright (c) 2026 Golioth
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "zephyr/sys/atomic_types.h"
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(conn_monitor, CONFIG_EXAMPLE_HTTP_LOG_LEVEL);

#include <zephyr/kernel.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/conn_mgr_monitor.h>

static K_SEM_DEFINE(connected, 0, 1);

static atomic_t conn_flags;
#define CONNECTED_BIT BIT(0)

static void l4_event_handler(uint64_t event,
                             struct net_if *iface,
                             void *info,
                             size_t info_length,
                             void *user_data)
{
    switch (event)
    {
        case NET_EVENT_L4_CONNECTED:
            k_sem_give(&connected);
            atomic_set_bit(&conn_flags, CONNECTED_BIT);
            break;
        case NET_EVENT_L4_DISCONNECTED:
            k_sem_take(&connected, K_NO_WAIT);
            atomic_clear_bit(&conn_flags, CONNECTED_BIT);
            break;
        default:
            break;
    }
}

NET_MGMT_REGISTER_EVENT_HANDLER(l4_cb,
                                (NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED),
                                l4_event_handler,
                                NULL);

void wait_for_network(void)
{
    conn_mgr_mon_resend_status();
    LOG_INF("Waiting for network...");
	k_sem_take(&connected, K_FOREVER);
    LOG_INF("Network connected");
}

bool is_connected(void)
{
    return atomic_test_bit(&conn_flags, CONNECTED_BIT);
}
