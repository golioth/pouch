/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/drivers/gpio.h>

#include <golioth/client.h>
#include <golioth/gateway.h>
#include <samples/common/sample_credentials.h>

#include <pouch/transport/gatt/common/types.h>

#include <pouch/gateway/bt/bond.h>
#include <pouch/gateway/bt/connect.h>
#include <pouch/gateway/cert.h>
#include <pouch/gateway/downlink.h>
#include <pouch/gateway/uplink.h>

#include "scan.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main);

struct sync_data
{
    struct k_work_delayable work;
    struct bt_conn *conn;
    int counter;
};

static struct sync_data sync_data;

struct golioth_client *client;

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET_OR(DT_ALIAS(sw0), gpios, {});
static struct gpio_callback button_cb_data;
static struct bt_conn *default_conn;

static const k_timeout_t bonding_timeout = K_SECONDS(30);
static const bt_security_t bt_security =
    IS_ENABLED(CONFIG_POUCH_GATEWAY_GATT_SCAN_FILTER_BONDED) ? BT_SECURITY_L4 : BT_SECURITY_L2;

static void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    if (default_conn)
    {
        LOG_INF("Confirming passkey");
        bt_conn_auth_passkey_confirm(default_conn);
    }
    else if (pouch_gateway_bonding_is_enabled())
    {
        LOG_WRN("Bonding already enabled");
    }
    else
    {
        pouch_gateway_bonding_enable(bonding_timeout);
    }
}

#ifdef CONFIG_POUCH_GATEWAY_CLOUD

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
    LOG_INF("Golioth client %s", is_connected ? "connected" : "disconnected");
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

#ifdef CONFIG_NRF_MODEM
#include <modem/lte_lc.h>
static void lte_handler(const struct lte_lc_evt *const evt)
{
    if (evt->type == LTE_LC_EVT_NW_REG_STATUS)
    {

        if ((evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_HOME)
            || (evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_ROAMING))
        {

            if (!client)
            {
                connect_golioth_client();
            }
        }
    }
}
#endif /* CONFIG_NRF_MODEM */

static void connect_to_cloud(void)
{
#if defined(CONFIG_NRF_MODEM)
    LOG_INF("Connecting to LTE, this may take some time...");
    lte_lc_connect_async(lte_handler);
#else
#if defined(CONFIG_NET_L2_ETHERNET) && defined(CONFIG_NET_DHCPV4)
    net_dhcpv4_start(net_if_get_default());
#endif
    connect_golioth_client();
#endif

    LOG_INF("Waiting for network connection");
    k_sem_take(&connected, K_FOREVER);
}

#else /* CONFIG_POUCH_GATEWAY_CLOUD */

static inline void connect_to_cloud(void) {}

#endif /* CONFIG_POUCH_GATEWAY_CLOUD */

static void bt_connected(struct bt_conn *conn, uint8_t err)
{
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    if (err)
    {
        LOG_ERR("Failed to connect to %s %u %s", addr, err, bt_hci_err_to_str(err));

        bt_conn_unref(conn);

        custom_scan_start();
        return;
    }

    LOG_INF("Connected: %s", addr);
    default_conn = conn;

    err = bt_conn_set_security(conn, bt_security);
    if (err)
    {
        LOG_ERR("Failed to set security (%d).", err);

        bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
        return;
    }
}

static void bt_disconnected(struct bt_conn *conn, uint8_t reason)
{
    char addr[BT_ADDR_LE_STR_LEN];

    default_conn = NULL;

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_INF("Disconnected: %s, reason 0x%02x %s", addr, reason, bt_hci_err_to_str(reason));

    pouch_gateway_bt_stop(conn);

    bt_conn_unref(conn);

    custom_scan_start();
}

static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
    if (err)
    {
        LOG_ERR("BT security change failed. Current level: %d, err: %s(%u)",
                level,
                bt_security_err_to_str(err),
                err);

        struct bt_conn_info info;
        bt_conn_get_info(conn, &info);

        bt_unpair(info.id, info.le.dst);
        bt_conn_disconnect(conn, BT_HCI_ERR_INSUFFICIENT_SECURITY);
    }
    else
    {
        LOG_INF("BT security changed to level %u", level);

        sync_data.conn = conn;
        sync_data.counter = 0;

        k_work_schedule(&sync_data.work, K_NO_WAIT);
    }
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = bt_connected,
    .disconnected = bt_disconnected,
    .security_changed = security_changed,
};

static void auth_cancel(struct bt_conn *conn)
{
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    LOG_INF("Pairing cancelled: %s", addr);
}

static void auth_passkey_confirm(struct bt_conn *conn, unsigned int passkey)
{
    char addr[BT_ADDR_LE_STR_LEN];
    char passkey_str[7];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    snprintk(passkey_str, 7, "%06u", passkey);

    LOG_INF("Confirm passkey for %s: %s", addr, passkey_str);

    if (IS_ENABLED(CONFIG_SAMPLE_POUCH_GATEWAY_BT_AUTO_CONFIRM))
    {
        LOG_INF("Confirming passkey");
        bt_conn_auth_passkey_confirm(conn);
    }
}

static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
    char addr[BT_ADDR_LE_STR_LEN];
    char passkey_str[7];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    snprintk(passkey_str, 7, "%06u", passkey);

    LOG_INF("Passkey for %s: %s", addr, passkey_str);
}

static struct bt_conn_auth_cb auth_cb = {
    .cancel = auth_cancel,

    .passkey_confirm =
        IS_ENABLED(CONFIG_POUCH_GATEWAY_GATT_SCAN_FILTER_BONDED) ? auth_passkey_confirm : NULL,
    .passkey_display =
        IS_ENABLED(CONFIG_POUCH_GATEWAY_GATT_SCAN_FILTER_BONDED) ? auth_passkey_display : NULL,
};

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
    LOG_INF("Pairing Complete");
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
    LOG_WRN("Pairing Failed (%d). Disconnecting.", reason);

    bt_conn_disconnect(conn,
                       (reason == BT_SECURITY_ERR_PAIR_NOT_ALLOWED) ? BT_HCI_ERR_PAIRING_NOT_ALLOWED
                                                                    : BT_HCI_ERR_AUTH_FAIL);
}

static struct bt_conn_auth_info_cb auth_info_cb = {
    .pairing_complete = pairing_complete,
    .pairing_failed = pairing_failed,
};

static void sync_start_handler(struct k_work *work)
{
    pouch_gateway_bt_start(sync_data.conn);
}

void pouch_gateway_bt_finished(struct bt_conn *conn)
{
    sync_data.counter++;

    if (sync_data.counter > 1)
    {
        LOG_INF("Disconnecting");
        bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    }
    else
    {
        LOG_INF("Start sync once again in 5s");
        k_work_schedule(&sync_data.work, K_SECONDS(5));
    }
}

int main(void)
{
    int err;

    LOG_INF("Gateway Version: " STRINGIFY(GIT_DESCRIBE));
    LOG_INF("Pouch BLE Transport Protocol Version: %d", POUCH_GATT_VERSION);

    k_work_init_delayable(&sync_data.work, sync_start_handler);

    if (DT_HAS_ALIAS(sw0))
    {
        LOG_INF("Set up button at %s pin %d", button.port->name, button.pin);

        err = gpio_pin_configure_dt(&button, GPIO_INPUT);
        if (err < 0)
        {
            LOG_ERR("Could not initialize Button");
        }

        err = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
        if (err)
        {
            LOG_ERR("Error %d: failed to configure interrupt on %s pin %d",
                    err,
                    button.port->name,
                    button.pin);
            return 0;
        }

        gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
        gpio_add_callback(button.port, &button_cb_data);
    }

    connect_to_cloud();

    pouch_gateway_cert_module_on_connected(client);
    pouch_gateway_uplink_module_init(client);
    pouch_gateway_downlink_module_init(client);

    err = bt_enable(NULL);
    if (err)
    {
        LOG_ERR("Bluetooth init failed (err %d)", err);
        return err;
    }

    if (IS_ENABLED(CONFIG_BT_SMP))
    {
        bt_conn_auth_cb_register(&auth_cb);
        bt_conn_auth_info_cb_register(&auth_info_cb);
    }

    LOG_INF("Bluetooth initialized");

    if (IS_ENABLED(CONFIG_SAMPLE_POUCH_GATEWAY_BT_AUTO_BOND))
    {
        pouch_gateway_bonding_enable(K_FOREVER);
    }

    custom_scan_start();

#ifdef CONFIG_POUCH_GATEWAY_CLOUD
    while (true)
    {
        k_sem_take(&connected, K_FOREVER);
        pouch_gateway_cert_module_on_connected(client);
    }
#endif

    return 0;
}
