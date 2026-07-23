/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/ztest.h>

#include "broker.h"
#include "discover.h"
#include "transport/endpoints/broker/endpoints.h"
#include "transport/sar/packet.h"
#include "transport/sar/receiver.h"
#include "transport/sar/sender.h"

#define TEST_MTU 64
#define MAX_GATT_CALLS 16

struct fake_conn
{
    uint8_t index;
    int refs;
    bool connected;
};

static struct fake_conn connections[CONFIG_BT_MAX_CONN] = {
    {.index = 0, .connected = true},
    {.index = 1, .connected = true},
};
static struct fake_conn old_connection = {.index = 0, .connected = true};

static struct
{
    int discover_result;
    int subscribe_result;
    int unsubscribe_result;
    int write_result;
    bool unsubscribe_completes_synchronously;
    bool next_subscribe_write_pending;
    bool ref_returns_null;
    bool index_forbidden;

    atomic_t ref_calls;
    atomic_t unref_calls;
    atomic_t discover_calls;
    atomic_t subscribe_calls;
    atomic_t unsubscribe_calls;
    atomic_t cancel_calls;
    atomic_t write_calls;

    struct broker_bt_gatt_device *discover_device;
    broker_bt_gatt_discover_callback_t discover_callback;
    uint16_t handles_at_discovery[BROKER_BT_ATTRS][2];
    bool server_cert_provisioned_at_discovery;
    bool device_cert_provisioned_at_discovery;

    struct bt_conn *subscribe_conns[MAX_GATT_CALLS];
    struct bt_gatt_subscribe_params *subscribe_params[MAX_GATT_CALLS];
    struct bt_conn *unsubscribe_conns[MAX_GATT_CALLS];
    struct bt_gatt_subscribe_params *unsubscribe_params[MAX_GATT_CALLS];

    struct bt_conn *cancel_conn;
    void *cancel_params;
    struct bt_conn *write_conn;
    uint16_t write_handle;
    uint16_t write_length;
} bt_fake;

static struct
{
    atomic_t start_calls;
    atomic_t recv_calls;
    atomic_t send_calls;
    atomic_t end_calls;
    atomic_t successful_end_calls;
    atomic_t failed_end_calls;
} endpoint_fake;

static atomic_t on_end_calls;
static atomic_t rejected_on_end_calls;
static struct bt_conn *last_on_end_conn;
static int refs_at_on_end;

static struct bt_conn *bt_conn(struct fake_conn *conn)
{
    return (struct bt_conn *) conn;
}

static struct fake_conn *fake_conn(struct bt_conn *conn)
{
    return (struct fake_conn *) conn;
}

/* Model the pinned Zephyr gatt.c ordering for its internal CCC write-pending flag. */
static bool fake_ccc_write_pending(struct bt_gatt_subscribe_params *params)
{
    return atomic_test_bit(params->flags, BT_GATT_SUBSCRIBE_FLAG_WRITE_PENDING);
}

static void fake_ccc_write_pending_set(struct bt_gatt_subscribe_params *params)
{
    atomic_set_bit(params->flags, BT_GATT_SUBSCRIBE_FLAG_WRITE_PENDING);
}

static void fake_ccc_write_pending_clear(struct bt_gatt_subscribe_params *params)
{
    atomic_clear_bit(params->flags, BT_GATT_SUBSCRIBE_FLAG_WRITE_PENDING);
}

static bool fake_ccc_write_pending_take(struct bt_gatt_subscribe_params *params)
{
    return atomic_test_and_clear_bit(params->flags, BT_GATT_SUBSCRIBE_FLAG_WRITE_PENDING);
}

struct bt_conn *bt_conn_ref(struct bt_conn *conn)
{
    if (conn != NULL)
    {
        atomic_inc(&bt_fake.ref_calls);
        if (bt_fake.ref_returns_null)
        {
            return NULL;
        }

        fake_conn(conn)->refs++;
    }

    return conn;
}

void bt_conn_unref(struct bt_conn *conn)
{
    if (conn != NULL)
    {
        fake_conn(conn)->refs--;
        atomic_inc(&bt_fake.unref_calls);
    }
}

uint8_t bt_conn_index(const struct bt_conn *conn)
{
    zassert_not_null(conn, "bt_conn_index() must not be called with NULL");
    zassert_false(bt_fake.index_forbidden, "bt_conn_index() called after bt_conn_ref() failed");

    return ((const struct fake_conn *) conn)->index;
}

uint16_t bt_gatt_get_mtu(struct bt_conn *conn)
{
    ARG_UNUSED(conn);
    return TEST_MTU;
}

int bt_gatt_subscribe(struct bt_conn *conn, struct bt_gatt_subscribe_params *params)
{
    int index = atomic_inc(&bt_fake.subscribe_calls);

    if (index < MAX_GATT_CALLS)
    {
        bt_fake.subscribe_conns[index] = conn;
        bt_fake.subscribe_params[index] = params;
    }

    if (bt_fake.subscribe_result == 0 && bt_fake.next_subscribe_write_pending)
    {
        fake_ccc_write_pending_set(params);
        bt_fake.next_subscribe_write_pending = false;
    }

    return bt_fake.subscribe_result;
}

int bt_gatt_unsubscribe(struct bt_conn *conn, struct bt_gatt_subscribe_params *params)
{
    int index = atomic_inc(&bt_fake.unsubscribe_calls);

    if (index < MAX_GATT_CALLS)
    {
        bt_fake.unsubscribe_conns[index] = conn;
        bt_fake.unsubscribe_params[index] = params;
    }

    if (bt_fake.unsubscribe_result == 0 && fake_ccc_write_pending_take(params))
    {
        params->notify(conn, params, NULL, 0);
        if (params->subscribe != NULL)
        {
            params->subscribe(conn, BT_ATT_ERR_UNLIKELY, params);
        }

        params->value = 0;
        fake_ccc_write_pending_set(params);
    }
    else if (bt_fake.unsubscribe_result == 0 && bt_fake.unsubscribe_completes_synchronously)
    {
        params->notify(conn, params, NULL, 0);
    }
    else if (bt_fake.unsubscribe_result == 0)
    {
        params->value = 0;
        fake_ccc_write_pending_set(params);
    }

    return bt_fake.unsubscribe_result;
}

void bt_gatt_cancel(struct bt_conn *conn, void *params)
{
    atomic_inc(&bt_fake.cancel_calls);
    bt_fake.cancel_conn = conn;
    bt_fake.cancel_params = params;
}

int bt_gatt_write_without_response_cb(struct bt_conn *conn,
                                      uint16_t handle,
                                      const void *data,
                                      uint16_t length,
                                      bool sign,
                                      bt_gatt_complete_func_t func,
                                      void *user_data)
{
    ARG_UNUSED(data);
    ARG_UNUSED(sign);
    ARG_UNUSED(func);
    ARG_UNUSED(user_data);

    atomic_inc(&bt_fake.write_calls);
    bt_fake.write_conn = conn;
    bt_fake.write_handle = handle;
    bt_fake.write_length = length;

    return bt_fake.write_result;
}

int gateway_bt_discover(struct broker_bt_gatt_device *device,
                        broker_bt_gatt_discover_callback_t on_complete)
{
    atomic_inc(&bt_fake.discover_calls);
    bt_fake.discover_device = device;
    bt_fake.discover_callback = on_complete;

    for (enum broker_bt_attr attr = 0; attr < BROKER_BT_ATTRS; attr++)
    {
        bt_fake.handles_at_discovery[attr][0] = device->chars[attr].handle.value;
        bt_fake.handles_at_discovery[attr][1] = device->chars[attr].handle.ccc;
    }
    bt_fake.server_cert_provisioned_at_discovery = device->node.server_cert_provisioned;
    bt_fake.device_cert_provisioned_at_discovery = device->node.device_cert_provisioned;

    return bt_fake.discover_result;
}

static int endpoint_start(struct pouch_bearer *bearer)
{
    ARG_UNUSED(bearer);
    atomic_inc(&endpoint_fake.start_calls);
    return 0;
}

static int endpoint_recv(struct pouch_bearer *bearer, const void *buf, size_t len)
{
    ARG_UNUSED(bearer);
    ARG_UNUSED(buf);
    ARG_UNUSED(len);
    atomic_inc(&endpoint_fake.recv_calls);
    return 0;
}

static enum pouch_result endpoint_send(struct pouch_bearer *bearer, void *buf, size_t *len)
{
    ARG_UNUSED(bearer);
    ARG_UNUSED(buf);
    atomic_inc(&endpoint_fake.send_calls);
    *len = 0;
    return POUCH_NO_MORE_DATA;
}

static void endpoint_end(struct pouch_bearer *bearer, bool success)
{
    ARG_UNUSED(bearer);
    atomic_inc(&endpoint_fake.end_calls);
    atomic_inc(success ? &endpoint_fake.successful_end_calls : &endpoint_fake.failed_end_calls);
}

const struct pouch_endpoint broker_endpoint_info = {
    .start = endpoint_start,
    .end = endpoint_end,
    .recv = endpoint_recv,
};

const struct pouch_endpoint broker_endpoint_device_cert = {
    .start = endpoint_start,
    .end = endpoint_end,
    .recv = endpoint_recv,
};

const struct pouch_endpoint broker_endpoint_server_cert = {
    .start = endpoint_start,
    .end = endpoint_end,
    .send = endpoint_send,
};

const struct pouch_endpoint broker_endpoint_uplink = {
    .start = endpoint_start,
    .end = endpoint_end,
    .recv = endpoint_recv,
};

const struct pouch_endpoint broker_endpoint_downlink = {
    .start = endpoint_start,
    .end = endpoint_end,
    .send = endpoint_send,
};

static void on_end(struct bt_conn *conn)
{
    last_on_end_conn = conn;
    refs_at_on_end = fake_conn(conn)->refs;
    atomic_inc(&on_end_calls);
}

static void rejected_on_end(struct bt_conn *conn)
{
    ARG_UNUSED(conn);
    atomic_inc(&rejected_on_end_calls);
}

struct saved_characteristic
{
    enum characteristic_type type;
    union
    {
        struct pouch_sender *sender;
        struct pouch_receiver *receiver;
    };
};

static void reset_device(struct broker_bt_gatt_device *device)
{
    struct saved_characteristic saved[BROKER_BT_ATTRS];

    if (device->cleanup_work_initialized)
    {
        struct k_work_sync sync;

        (void) k_work_cancel_delayable_sync(&device->cleanup_work, &sync);
    }

    for (enum broker_bt_attr attr = 0; attr < BROKER_BT_ATTRS; attr++)
    {
        struct characteristic *characteristic = &device->chars[attr];

        saved[attr].type = characteristic->type;
        if (characteristic->type == CHAR_RECEIVER)
        {
            struct pouch_receiver *receiver = characteristic->receiver;
            const struct pouch_endpoint *endpoint = receiver->endpoint;

            saved[attr].receiver = receiver;
            if (receiver->work.handler != NULL)
            {
                struct k_work_sync sync;

                (void) k_work_cancel_delayable_sync(&receiver->work.dwork, &sync);
            }
            *receiver = (struct pouch_receiver){.endpoint = endpoint};
        }
        else
        {
            struct pouch_sender *sender = characteristic->sender;
            const struct pouch_endpoint *endpoint = sender->endpoint;

            saved[attr].sender = sender;
            free(sender->buf);
            *sender = (struct pouch_sender){.endpoint = endpoint};
        }
    }

    memset(device, 0, sizeof(*device));
    for (enum broker_bt_attr attr = 0; attr < BROKER_BT_ATTRS; attr++)
    {
        device->chars[attr].type = saved[attr].type;
        if (saved[attr].type == CHAR_RECEIVER)
        {
            device->chars[attr].receiver = saved[attr].receiver;
        }
        else
        {
            device->chars[attr].sender = saved[attr].sender;
        }
    }
}

static void reset(void *fixture)
{
    ARG_UNUSED(fixture);

    bt_fake.index_forbidden = false;
    for (size_t index = 0; index < ARRAY_SIZE(connections); index++)
    {
        reset_device(broker_bt_gatt_device(bt_conn(&connections[index])));
    }

    memset(&bt_fake, 0, sizeof(bt_fake));
    memset(&endpoint_fake, 0, sizeof(endpoint_fake));
    atomic_set(&on_end_calls, 0);
    atomic_set(&rejected_on_end_calls, 0);
    last_on_end_conn = NULL;
    refs_at_on_end = 0;

    for (size_t index = 0; index < ARRAY_SIZE(connections); index++)
    {
        connections[index] = (struct fake_conn){
            .index = index,
            .connected = true,
        };
    }
    old_connection = (struct fake_conn){
        .index = 0,
        .connected = true,
    };
}

static void flush_cleanup(struct broker_bt_gatt_device *device)
{
    if (device->cleanup_work_initialized)
    {
        struct k_work_sync sync;

        (void) k_work_flush_delayable(&device->cleanup_work, &sync);
    }
}

static void complete_discovery(struct broker_bt_gatt_device *device, bool success)
{
    zassert_equal(bt_fake.discover_device, device);
    zassert_not_null(bt_fake.discover_callback);

    if (success)
    {
        for (enum broker_bt_attr attr = 0; attr < BROKER_BT_ATTRS; attr++)
        {
            device->chars[attr].handle.value = 0x10 + (2 * attr);
            device->chars[attr].handle.ccc = 0x11 + (2 * attr);
        }
    }

    bt_fake.discover_callback(device, success);
}

static struct broker_bt_gatt_device *start_info_session(struct bt_conn *conn)
{
    struct broker_bt_gatt_device *device = broker_bt_gatt_device(conn);

    zassert_ok(pouch_gateway_bt_start(conn, on_end));
    complete_discovery(device, true);
    zassert_equal(atomic_get(&bt_fake.subscribe_calls), 1);
    zassert_equal(bt_fake.subscribe_params[0], &device->chars[BROKER_BT_ATTR_INFO].sub_params);

    return device;
}

static uint8_t notify(struct bt_conn *conn,
                      struct bt_gatt_subscribe_params *params,
                      const void *data,
                      uint16_t length)
{
    zassert_not_null(params);
    zassert_not_null(params->notify);
    return params->notify(conn, params, data, length);
}

static void terminal_notification(struct bt_conn *conn, struct bt_gatt_subscribe_params *params)
{
    params->value = 0;
    fake_ccc_write_pending_clear(params);
    zassert_equal(notify(conn, params, NULL, 0), BT_GATT_ITER_STOP);
    if (params->subscribe != NULL)
    {
        params->subscribe(conn, 0, params);
    }
    flush_cleanup(broker_bt_gatt_device(conn));
}

static void complete_unsubscribe_response(struct bt_conn *conn,
                                          struct bt_gatt_subscribe_params *params)
{
    zassert_true(fake_ccc_write_pending_take(params));
    terminal_notification(conn, params);
}

static void drop_unsubscribe_response(struct bt_gatt_subscribe_params *params)
{
    zassert_true(fake_ccc_write_pending_take(params));
}

static void complete_receiver_transfer(struct bt_conn *conn,
                                       struct bt_gatt_subscribe_params *params)
{
    static const uint8_t payload[] = {0xaa};
    uint8_t buffer[8];
    size_t length = sizeof(buffer);
    struct pouch_sar_tx_pkt packet = {
        .seq = 0,
        .flags = POUCH_SAR_TX_PKT_FLAG_FIRST | POUCH_SAR_TX_PKT_FLAG_LAST,
        .data = payload,
        .len = sizeof(payload),
    };

    zassert_ok(pouch_sar_tx_pkt_encode(&packet, buffer, &length));
    zassert_equal(notify(conn, params, buffer, length), BT_GATT_ITER_CONTINUE);

    /* FIN without the internal IDLE marker encodes an ACK on the wire. */
    packet = (struct pouch_sar_tx_pkt){
        .flags = POUCH_SAR_TX_PKT_FLAG_FIN,
    };
    length = sizeof(buffer);
    zassert_ok(pouch_sar_tx_pkt_encode(&packet, buffer, &length));
    zassert_equal(notify(conn, params, buffer, length), BT_GATT_ITER_STOP);
}

static void complete_sender_transfer(struct bt_conn *conn, struct bt_gatt_subscribe_params *params)
{
    uint8_t buffer[POUCH_SAR_RX_PKT_LEN];
    struct pouch_sar_rx_pkt ack = {
        .code = POUCH_RECEIVER_CODE_ACK,
        .seq = POUCH_SAR_SEQ_MAX,
        .window = 1,
    };

    pouch_sar_rx_pkt_encode(&ack, buffer);
    zassert_equal(notify(conn, params, buffer, sizeof(buffer)), BT_GATT_ITER_CONTINUE);

    ack.seq = 0;
    ack.window = 0;
    pouch_sar_rx_pkt_encode(&ack, buffer);
    zassert_equal(notify(conn, params, buffer, sizeof(buffer)), BT_GATT_ITER_STOP);
}

static struct broker_bt_gatt_device *start_link_session(struct bt_conn *conn)
{
    struct broker_bt_gatt_device *device = start_info_session(conn);
    struct bt_gatt_subscribe_params *info_params = bt_fake.subscribe_params[0];

    device->node.server_cert_provisioned = true;
    device->node.device_cert_provisioned = true;
    complete_receiver_transfer(conn, info_params);
    terminal_notification(conn, info_params);

    zassert_equal(atomic_get(&bt_fake.subscribe_calls), 3);
    zassert_equal(bt_fake.subscribe_params[1], &device->chars[BROKER_BT_ATTR_DOWNLINK].sub_params);
    zassert_equal(bt_fake.subscribe_params[2], &device->chars[BROKER_BT_ATTR_UPLINK].sub_params);

    return device;
}

ZTEST_SUITE(gatt_broker, NULL, NULL, reset, NULL, NULL);

ZTEST(gatt_broker, test_null_arguments_are_rejected)
{
    struct bt_conn *conn = bt_conn(&connections[0]);

    zassert_equal(pouch_gateway_bt_start(NULL, on_end), -EINVAL);
    zassert_equal(pouch_gateway_bt_start(conn, NULL), -EINVAL);
    zassert_equal(atomic_get(&bt_fake.discover_calls), 0);
    zassert_equal(atomic_get(&bt_fake.ref_calls), 0);
    zassert_equal(atomic_get(&on_end_calls), 0);
}

ZTEST(gatt_broker, test_dead_connection_is_rejected_before_indexing)
{
    struct bt_conn *conn = bt_conn(&connections[0]);

    bt_fake.ref_returns_null = true;
    bt_fake.index_forbidden = true;
    zassert_equal(pouch_gateway_bt_start(conn, rejected_on_end), -ENOTCONN);
    bt_fake.index_forbidden = false;

    zassert_equal(atomic_get(&bt_fake.ref_calls), 1);
    zassert_equal(atomic_get(&bt_fake.unref_calls), 0);
    zassert_equal(atomic_get(&bt_fake.discover_calls), 0);
    zassert_equal(atomic_get(&rejected_on_end_calls), 0);
    zassert_equal(connections[0].refs, 0);
    zassert_equal(atomic_get(&broker_bt_gatt_device(conn)->state), BROKER_SESSION_IDLE);
}

ZTEST(gatt_broker, test_synchronous_discovery_error_rejects_without_callback)
{
    struct bt_conn *conn = bt_conn(&connections[0]);
    struct broker_bt_gatt_device *device = broker_bt_gatt_device(conn);

    bt_fake.discover_result = -EIO;
    zassert_equal(pouch_gateway_bt_start(conn, rejected_on_end), -EIO);
    zassert_equal(atomic_get(&bt_fake.discover_calls), 1);
    zassert_equal(atomic_get(&bt_fake.ref_calls), 1);
    zassert_equal(atomic_get(&bt_fake.unref_calls), 1);
    zassert_equal(connections[0].refs, 0);

    flush_cleanup(device);
    k_sleep(K_MSEC(2));
    flush_cleanup(device);
    zassert_equal(atomic_get(&rejected_on_end_calls), 0);
    zassert_false(k_work_delayable_is_pending(&device->cleanup_work));

    bt_fake.discover_result = 0;
    zassert_ok(pouch_gateway_bt_start(conn, on_end));
    zassert_equal(atomic_get(&bt_fake.discover_calls), 2);
    complete_discovery(device, false);
    flush_cleanup(device);

    zassert_equal(atomic_get(&rejected_on_end_calls), 0);
    zassert_equal(atomic_get(&on_end_calls), 1);
    zassert_equal(atomic_get(&bt_fake.ref_calls), 2);
    zassert_equal(atomic_get(&bt_fake.unref_calls), 2);
    zassert_equal(connections[0].refs, 0);
}

ZTEST(gatt_broker, test_overlapping_start_is_busy)
{
    struct bt_conn *conn = bt_conn(&connections[0]);
    struct bt_conn *overlap = bt_conn(&old_connection);
    struct broker_bt_gatt_device *device = broker_bt_gatt_device(conn);

    zassert_ok(pouch_gateway_bt_start(conn, on_end));
    zassert_equal(pouch_gateway_bt_start(overlap, rejected_on_end), -EBUSY);
    zassert_equal(atomic_get(&bt_fake.discover_calls), 1);
    zassert_equal(atomic_get(&rejected_on_end_calls), 0);
    zassert_equal(atomic_get(&bt_fake.ref_calls), 2);
    zassert_equal(atomic_get(&bt_fake.unref_calls), 1);
    zassert_equal(old_connection.refs, 0);

    complete_discovery(device, false);
    flush_cleanup(device);
    zassert_equal(atomic_get(&on_end_calls), 1);
    zassert_equal(atomic_get(&rejected_on_end_calls), 0);
    zassert_equal(atomic_get(&bt_fake.unref_calls), 2);
}

ZTEST(gatt_broker, test_accepted_session_holds_connection_reference)
{
    struct bt_conn *conn = bt_conn(&connections[0]);
    struct broker_bt_gatt_device *device = broker_bt_gatt_device(conn);

    zassert_ok(pouch_gateway_bt_start(conn, on_end));
    zassert_equal(atomic_get(&bt_fake.ref_calls), 1);
    zassert_equal(atomic_get(&bt_fake.unref_calls), 0);
    zassert_equal(connections[0].refs, 1);

    complete_discovery(device, false);
    flush_cleanup(device);
    zassert_equal(atomic_get(&on_end_calls), 1);
    zassert_equal(last_on_end_conn, conn);
    zassert_equal(refs_at_on_end, 1);
    zassert_equal(atomic_get(&bt_fake.ref_calls), 1);
    zassert_equal(atomic_get(&bt_fake.unref_calls), 1);
    zassert_equal(connections[0].refs, 0);
}

ZTEST(gatt_broker, test_repeated_discovery_failure_finishes_once)
{
    struct bt_conn *conn = bt_conn(&connections[0]);
    struct broker_bt_gatt_device *device = broker_bt_gatt_device(conn);
    broker_bt_gatt_discover_callback_t complete;

    zassert_ok(pouch_gateway_bt_start(conn, on_end));
    complete = bt_fake.discover_callback;
    complete(device, false);
    complete(device, false);
    flush_cleanup(device);
    zassert_equal(atomic_get(&on_end_calls), 1);

    complete(device, false);
    flush_cleanup(device);
    zassert_equal(atomic_get(&on_end_calls), 1);
    zassert_equal(atomic_get(&bt_fake.unref_calls), 1);
}

ZTEST(gatt_broker, test_malformed_notification_finishes_once)
{
    static const uint8_t malformed[] = {0xff};
    struct bt_conn *conn = bt_conn(&connections[0]);
    struct broker_bt_gatt_device *device = start_info_session(conn);
    struct bt_gatt_subscribe_params *params = bt_fake.subscribe_params[0];
    bt_gatt_notify_func_t notify_callback = params->notify;

    zassert_equal(notify(conn, params, malformed, sizeof(malformed)), BT_GATT_ITER_STOP);
    flush_cleanup(device);
    zassert_equal(atomic_get(&on_end_calls), 0);

    terminal_notification(conn, params);
    flush_cleanup(device);
    zassert_equal(atomic_get(&on_end_calls), 1);

    zassert_equal(notify_callback(conn, params, malformed, sizeof(malformed)), BT_GATT_ITER_STOP);
    zassert_equal(notify_callback(conn, params, malformed, sizeof(malformed)), BT_GATT_ITER_STOP);
    flush_cleanup(device);
    zassert_equal(atomic_get(&on_end_calls), 1);
    zassert_equal(atomic_get(&bt_fake.unref_calls), 1);
}

ZTEST(gatt_broker, test_reentrant_link_close_finishes_once)
{
    static const uint8_t malformed[] = {0xff};
    struct bt_conn *conn = bt_conn(&connections[0]);
    struct broker_bt_gatt_device *device = start_link_session(conn);
    struct bt_gatt_subscribe_params *downlink = bt_fake.subscribe_params[1];
    struct bt_gatt_subscribe_params *uplink = bt_fake.subscribe_params[2];

    zassert_equal(notify(conn, uplink, malformed, sizeof(malformed)), BT_GATT_ITER_STOP);
    flush_cleanup(device);
    zassert_equal(atomic_get(&on_end_calls), 0);
    zassert_equal(atomic_get(&endpoint_fake.failed_end_calls), 2);

    terminal_notification(conn, uplink);
    flush_cleanup(device);
    zassert_equal(atomic_get(&on_end_calls), 0);
    terminal_notification(conn, downlink);
    flush_cleanup(device);
    zassert_equal(atomic_get(&on_end_calls), 1);
    zassert_equal(atomic_get(&bt_fake.unref_calls), 1);
}

ZTEST(gatt_broker, test_successful_operation_finishes_once)
{
    struct bt_conn *conn = bt_conn(&connections[0]);
    struct broker_bt_gatt_device *device = start_link_session(conn);
    struct bt_gatt_subscribe_params *downlink = bt_fake.subscribe_params[1];
    struct bt_gatt_subscribe_params *uplink = bt_fake.subscribe_params[2];

    complete_sender_transfer(conn, downlink);
    terminal_notification(conn, downlink);
    zassert_equal(atomic_get(&on_end_calls), 0);

    complete_receiver_transfer(conn, uplink);
    terminal_notification(conn, uplink);
    flush_cleanup(device);
    zassert_equal(atomic_get(&on_end_calls), 1);
    zassert_equal(atomic_get(&endpoint_fake.successful_end_calls), 3);
    zassert_equal(atomic_get(&bt_fake.unref_calls), 1);
}

ZTEST(gatt_broker, test_error_waits_for_gatt_terminal_callback)
{
    static const uint8_t malformed[] = {0xff};
    struct bt_conn *conn = bt_conn(&connections[0]);
    struct broker_bt_gatt_device *device = start_info_session(conn);
    struct bt_gatt_subscribe_params *params = bt_fake.subscribe_params[0];

    zassert_equal(notify(conn, params, malformed, sizeof(malformed)), BT_GATT_ITER_STOP);
    flush_cleanup(device);
    zassert_equal(atomic_get(&on_end_calls), 0);
    zassert_equal(connections[0].refs, 1);

    terminal_notification(conn, params);
    flush_cleanup(device);
    zassert_equal(atomic_get(&on_end_calls), 1);
    zassert_equal(connections[0].refs, 0);
}

ZTEST(gatt_broker, test_synchronous_unsubscribe_completion_does_not_stall)
{
    struct bt_conn *conn = bt_conn(&connections[0]);
    struct broker_bt_gatt_device *device = start_info_session(conn);
    struct characteristic *info = &device->chars[BROKER_BT_ATTR_INFO];

    bt_fake.unsubscribe_completes_synchronously = true;
    info->bearer.close(&info->bearer, false);
    flush_cleanup(device);

    zassert_equal(atomic_get(&bt_fake.unsubscribe_calls), 1);
    zassert_equal(atomic_get(&on_end_calls), 1);
    zassert_false(atomic_test_bit(&info->flags, BROKER_CHAR_UNSUBSCRIBE_PENDING));
    zassert_false(k_work_delayable_is_pending(&device->cleanup_work));

    k_sleep(K_MSEC(2));
    zassert_equal(atomic_get(&bt_fake.unsubscribe_calls), 1);
    zassert_equal(atomic_get(&on_end_calls), 1);
    zassert_false(k_work_delayable_is_pending(&device->cleanup_work));
}

ZTEST(gatt_broker, test_pending_subscribe_cancel_waits_for_final_ccc_response)
{
    struct bt_conn *conn = bt_conn(&connections[0]);
    struct broker_bt_gatt_device *device = start_info_session(conn);
    struct bt_gatt_subscribe_params *info = bt_fake.subscribe_params[0];
    struct characteristic *server_cert_characteristic;
    struct bt_gatt_subscribe_params *server_cert;

    bt_fake.next_subscribe_write_pending = true;
    complete_receiver_transfer(conn, info);
    terminal_notification(conn, info);
    zassert_equal(atomic_get(&bt_fake.subscribe_calls), 2);

    server_cert = bt_fake.subscribe_params[1];
    server_cert_characteristic = &device->chars[BROKER_BT_ATTR_SERVER_CERT];
    complete_sender_transfer(conn, server_cert);
    zassert_true(fake_ccc_write_pending(server_cert));

    zassert_ok(bt_gatt_unsubscribe(conn, server_cert));
    flush_cleanup(device);

    zassert_equal(atomic_get(&bt_fake.subscribe_calls), 2);
    zassert_equal(atomic_get(&on_end_calls), 0);
    zassert_equal(atomic_get(&bt_fake.unref_calls), 0);
    zassert_equal(device->conn, conn);
    zassert_equal(atomic_get(&device->state), BROKER_SESSION_RUNNING);
    zassert_false(
        atomic_test_bit(&server_cert_characteristic->flags, BROKER_CHAR_CCC_SUBSCRIBE_PENDING));
    zassert_true(
        atomic_test_bit(&server_cert_characteristic->flags, BROKER_CHAR_CCC_UNSUBSCRIBE_PENDING));
    zassert_true(atomic_test_bit(&server_cert_characteristic->flags, BROKER_CHAR_GATT_TERMINATED));
    zassert_equal(pouch_gateway_bt_start(conn, rejected_on_end), -EBUSY);

    complete_unsubscribe_response(conn, server_cert);
    flush_cleanup(device);

    zassert_equal(atomic_get(&on_end_calls), 1);
    zassert_equal(atomic_get(&rejected_on_end_calls), 0);
    zassert_equal(atomic_get(&bt_fake.ref_calls), 2);
    zassert_equal(atomic_get(&bt_fake.unref_calls), 2);
    zassert_equal(connections[0].refs, 0);
    zassert_equal(atomic_get(&server_cert_characteristic->flags), 0);
}

ZTEST(gatt_broker, test_final_ccc_error_terminates_successful_transfer)
{
    struct bt_conn *conn = bt_conn(&connections[0]);
    struct broker_bt_gatt_device *device = start_info_session(conn);
    struct bt_gatt_subscribe_params *info = bt_fake.subscribe_params[0];

    complete_receiver_transfer(conn, info);
    zassert_ok(bt_gatt_unsubscribe(conn, info));
    flush_cleanup(device);
    zassert_true(fake_ccc_write_pending(info));

    fake_ccc_write_pending_clear(info);
    info->subscribe(conn, BT_ATT_ERR_UNLIKELY, info);
    flush_cleanup(device);

    zassert_equal(atomic_get(&bt_fake.subscribe_calls), 1);
    zassert_equal(atomic_get(&on_end_calls), 1);
    zassert_equal(atomic_get(&bt_fake.unref_calls), 1);
    zassert_equal(connections[0].refs, 0);
    zassert_equal(atomic_get(&device->state), BROKER_SESSION_IDLE);
}

ZTEST(gatt_broker, test_final_unsubscribe_request_error_terminates_successful_transfer)
{
    struct bt_conn *conn = bt_conn(&connections[0]);
    struct broker_bt_gatt_device *device = start_info_session(conn);
    struct bt_gatt_subscribe_params *info = bt_fake.subscribe_params[0];

    bt_fake.unsubscribe_result = -EINVAL;
    complete_receiver_transfer(conn, info);
    k_sleep(K_MSEC(5));
    flush_cleanup(device);

    for (int attempt = 0; attempt < 5 && k_work_delayable_is_pending(&device->cleanup_work);
         attempt++)
    {
        k_sleep(K_MSEC(1));
        flush_cleanup(device);
    }

    zassert_equal(atomic_get(&bt_fake.subscribe_calls), 1);
    zassert_equal(atomic_get(&on_end_calls), 1);
    zassert_equal(atomic_get(&bt_fake.unref_calls), 1);
    zassert_equal(connections[0].refs, 0);
    zassert_equal(atomic_get(&device->state), BROKER_SESSION_IDLE);
}

ZTEST(gatt_broker, test_missing_final_ccc_callbacks_are_reconciled)
{
    struct bt_conn *conn = bt_conn(&connections[0]);
    struct broker_bt_gatt_device *device = start_info_session(conn);
    struct characteristic *info = &device->chars[BROKER_BT_ATTR_INFO];

    info->bearer.close(&info->bearer, false);
    flush_cleanup(device);
    zassert_equal(atomic_get(&bt_fake.unsubscribe_calls), 1);
    zassert_true(fake_ccc_write_pending(&info->sub_params));
    zassert_equal(atomic_get(&on_end_calls), 0);

    drop_unsubscribe_response(&info->sub_params);
    k_sleep(K_MSEC(5));
    flush_cleanup(device);

    zassert_equal(atomic_get(&on_end_calls), 1);
    zassert_equal(atomic_get(&bt_fake.unref_calls), 1);
    zassert_equal(connections[0].refs, 0);
    zassert_equal(atomic_get(&info->flags), 0);
}

ZTEST(gatt_broker, test_missing_final_ccc_callbacks_terminate_successful_transfer)
{
    struct bt_conn *conn = bt_conn(&connections[0]);
    struct broker_bt_gatt_device *device = start_info_session(conn);
    struct bt_gatt_subscribe_params *info = bt_fake.subscribe_params[0];

    complete_receiver_transfer(conn, info);
    zassert_ok(bt_gatt_unsubscribe(conn, info));
    flush_cleanup(device);
    zassert_true(fake_ccc_write_pending(info));

    drop_unsubscribe_response(info);
    k_sleep(K_MSEC(5));
    flush_cleanup(device);

    for (int attempt = 0; attempt < 5 && k_work_delayable_is_pending(&device->cleanup_work);
         attempt++)
    {
        k_sleep(K_MSEC(1));
        flush_cleanup(device);
    }

    zassert_equal(atomic_get(&bt_fake.subscribe_calls), 1);
    zassert_equal(atomic_get(&on_end_calls), 1);
    zassert_equal(atomic_get(&bt_fake.unref_calls), 1);
    zassert_equal(connections[0].refs, 0);
    zassert_equal(atomic_get(&device->state), BROKER_SESSION_IDLE);
}

ZTEST(gatt_broker, test_auto_unsubscribe_transient_error_is_retried)
{
    struct bt_conn *conn = bt_conn(&connections[0]);
    struct broker_bt_gatt_device *device = start_info_session(conn);
    struct bt_gatt_subscribe_params *info = bt_fake.subscribe_params[0];

    device->node.server_cert_provisioned = true;
    device->node.device_cert_provisioned = true;
    bt_fake.unsubscribe_result = -ENOMEM;
    complete_receiver_transfer(conn, info);
    zassert_equal(bt_gatt_unsubscribe(conn, info), -ENOMEM);
    zassert_equal(atomic_get(&bt_fake.unsubscribe_calls), 1);
    zassert_equal(atomic_get(&bt_fake.subscribe_calls), 1);

    bt_fake.unsubscribe_result = 0;
    k_sleep(K_MSEC(5));
    flush_cleanup(device);

    zassert_equal(atomic_get(&bt_fake.unsubscribe_calls), 2);
    zassert_equal(atomic_get(&bt_fake.subscribe_calls), 1);
    zassert_true(fake_ccc_write_pending(info));
    zassert_equal(atomic_get(&on_end_calls), 0);

    complete_unsubscribe_response(conn, info);
    flush_cleanup(device);

    zassert_equal(atomic_get(&bt_fake.subscribe_calls), 3);
    zassert_equal(atomic_get(&on_end_calls), 0);
    zassert_equal(atomic_get(&device->state), BROKER_SESSION_RUNNING);

    bt_fake.unsubscribe_result = -ENOTCONN;
    broker_bt_disconnected(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    flush_cleanup(device);
    zassert_equal(atomic_get(&on_end_calls), 0);
    zassert_equal(atomic_get(&bt_fake.unref_calls), 1);
    zassert_equal(connections[0].refs, 0);
}

ZTEST(gatt_broker, test_cleanup_does_not_advance_characteristic_phase)
{
    struct bt_conn *conn = bt_conn(&connections[0]);
    struct broker_bt_gatt_device *device = start_info_session(conn);
    struct characteristic *info = &device->chars[BROKER_BT_ATTR_INFO];
    struct bt_gatt_subscribe_params *params = bt_fake.subscribe_params[0];

    info->bearer.close(&info->bearer, false);
    flush_cleanup(device);
    zassert_equal(atomic_get(&bt_fake.subscribe_calls), 1);
    zassert_equal(atomic_get(&on_end_calls), 0);

    terminal_notification(conn, params);
    flush_cleanup(device);
    zassert_equal(atomic_get(&bt_fake.subscribe_calls), 1);
    zassert_equal(atomic_get(&on_end_calls), 1);
}

ZTEST(gatt_broker, test_disconnect_suppresses_on_end)
{
    struct bt_conn *conn = bt_conn(&connections[0]);
    struct broker_bt_gatt_device *device = start_info_session(conn);

    bt_fake.unsubscribe_result = -ENOTCONN;
    connections[0].connected = false;
    broker_bt_disconnected(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    flush_cleanup(device);

    zassert_equal(atomic_get(&on_end_calls), 0);
    zassert_equal(atomic_get(&bt_fake.unsubscribe_calls), 1);
    zassert_equal(atomic_get(&bt_fake.ref_calls), 1);
    zassert_equal(atomic_get(&bt_fake.unref_calls), 1);
    zassert_equal(connections[0].refs, 0);
}

ZTEST(gatt_broker, test_null_unpair_notification_suppresses_on_end)
{
    struct bt_conn *conn = bt_conn(&connections[0]);
    struct broker_bt_gatt_device *device = start_info_session(conn);
    struct bt_gatt_subscribe_params *params = bt_fake.subscribe_params[0];

    zassert_equal(notify(NULL, params, NULL, 0), BT_GATT_ITER_STOP);
    flush_cleanup(device);

    zassert_equal(atomic_get(&on_end_calls), 0);
    zassert_equal(atomic_get(&bt_fake.ref_calls), 1);
    zassert_equal(atomic_get(&bt_fake.unref_calls), 1);
    zassert_equal(connections[0].refs, 0);
    zassert_equal(atomic_get(&device->state), BROKER_SESSION_IDLE);
}

ZTEST(gatt_broker, test_disconnect_while_closing_suppresses_on_end)
{
    static const uint8_t malformed[] = {0xff};
    struct bt_conn *conn = bt_conn(&connections[0]);
    struct broker_bt_gatt_device *device = start_info_session(conn);
    struct bt_gatt_subscribe_params *params = bt_fake.subscribe_params[0];

    zassert_equal(notify(conn, params, malformed, sizeof(malformed)), BT_GATT_ITER_STOP);
    flush_cleanup(device);
    zassert_equal(atomic_get(&on_end_calls), 0);
    zassert_equal(connections[0].refs, 1);

    connections[0].connected = false;
    broker_bt_disconnected(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    flush_cleanup(device);
    zassert_equal(atomic_get(&on_end_calls), 0);
    zassert_equal(connections[0].refs, 1);

    terminal_notification(conn, params);
    flush_cleanup(device);
    zassert_equal(atomic_get(&on_end_calls), 0);
    zassert_equal(atomic_get(&bt_fake.unref_calls), 1);
    zassert_equal(connections[0].refs, 0);
}

ZTEST(gatt_broker, test_closing_discovery_cancels_once)
{
    struct bt_conn *conn = bt_conn(&connections[0]);
    struct broker_bt_gatt_device *device = broker_bt_gatt_device(conn);
    broker_bt_gatt_discover_callback_t complete;

    zassert_ok(pouch_gateway_bt_start(conn, on_end));
    complete = bt_fake.discover_callback;
    connections[0].connected = false;
    broker_bt_disconnected(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    flush_cleanup(device);

    zassert_equal(atomic_get(&bt_fake.cancel_calls), 1);
    zassert_equal(bt_fake.cancel_conn, conn);
    zassert_equal(bt_fake.cancel_params, &device->discover.params);
    zassert_equal(atomic_get(&on_end_calls), 0);
    zassert_equal(connections[0].refs, 1);

    flush_cleanup(device);
    zassert_equal(atomic_get(&bt_fake.cancel_calls), 1);
    complete(device, false);
    flush_cleanup(device);
    zassert_equal(atomic_get(&bt_fake.cancel_calls), 1);
    zassert_equal(atomic_get(&on_end_calls), 0);
    zassert_equal(connections[0].refs, 0);
}

ZTEST(gatt_broker, test_old_disconnect_does_not_close_reused_slot)
{
    struct bt_conn *old_conn = bt_conn(&old_connection);
    struct bt_conn *conn = bt_conn(&connections[0]);
    struct broker_bt_gatt_device *device = broker_bt_gatt_device(conn);
    broker_bt_gatt_discover_callback_t old_complete;
    broker_bt_gatt_discover_callback_t current_complete;

    zassert_ok(pouch_gateway_bt_start(old_conn, on_end));
    old_complete = bt_fake.discover_callback;
    old_connection.connected = false;
    broker_bt_disconnected(old_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    flush_cleanup(device);
    zassert_equal(atomic_get(&bt_fake.cancel_calls), 1);
    old_complete(device, false);
    flush_cleanup(device);
    zassert_equal(old_connection.refs, 0);
    zassert_equal(atomic_get(&on_end_calls), 0);

    zassert_ok(pouch_gateway_bt_start(conn, on_end));
    current_complete = bt_fake.discover_callback;
    zassert_equal(connections[0].refs, 1);

    broker_bt_disconnected(old_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    flush_cleanup(device);
    zassert_equal(device->conn, conn);
    zassert_equal(atomic_get(&bt_fake.cancel_calls), 1);
    zassert_equal(connections[0].refs, 1);
    zassert_equal(atomic_get(&on_end_calls), 0);

    current_complete(device, false);
    flush_cleanup(device);
    zassert_equal(atomic_get(&on_end_calls), 1);
    zassert_equal(last_on_end_conn, conn);
    zassert_equal(connections[0].refs, 0);
}

ZTEST(gatt_broker, test_new_session_resets_peer_state)
{
    struct bt_conn *conn = bt_conn(&connections[0]);
    struct broker_bt_gatt_device *device = broker_bt_gatt_device(conn);

    for (enum broker_bt_attr attr = 0; attr < BROKER_BT_ATTRS; attr++)
    {
        device->chars[attr].handle.value = 0xa0 + attr;
        device->chars[attr].handle.ccc = 0xb0 + attr;
    }
    device->node.server_cert_provisioned = true;
    device->node.device_cert_provisioned = true;

    zassert_ok(pouch_gateway_bt_start(conn, on_end));
    for (enum broker_bt_attr attr = 0; attr < BROKER_BT_ATTRS; attr++)
    {
        zassert_equal(bt_fake.handles_at_discovery[attr][0], 0);
        zassert_equal(bt_fake.handles_at_discovery[attr][1], 0);
    }
    zassert_false(bt_fake.server_cert_provisioned_at_discovery);
    zassert_false(bt_fake.device_cert_provisioned_at_discovery);

    complete_discovery(device, false);
    flush_cleanup(device);
    zassert_equal(atomic_get(&on_end_calls), 1);
}

ZTEST(gatt_broker, test_late_bearer_activity_is_rejected)
{
    struct bt_conn *conn = bt_conn(&connections[0]);
    struct broker_bt_gatt_device *device = start_info_session(conn);
    struct characteristic *info = &device->chars[BROKER_BT_ATTR_INFO];
    struct bt_gatt_subscribe_params *params = bt_fake.subscribe_params[0];
    struct k_work_sync sync;
    int write_calls;
    int endpoint_end_calls;
    uint8_t byte = 0;

    info->bearer.close(&info->bearer, false);
    flush_cleanup(device);
    (void) k_work_cancel_delayable_sync(&info->receiver->work.dwork, &sync);
    write_calls = atomic_get(&bt_fake.write_calls);
    endpoint_end_calls = atomic_get(&endpoint_fake.end_calls);

    zassert_equal(info->bearer.send(&info->bearer, &byte, sizeof(byte)), -ENOTCONN);
    info->bearer.ready(&info->bearer);
    info->bearer.close(&info->bearer, false);
    k_sleep(K_MSEC(1));

    zassert_equal(atomic_get(&bt_fake.write_calls), write_calls);
    zassert_equal(atomic_get(&endpoint_fake.end_calls), endpoint_end_calls);
    zassert_false(k_work_delayable_is_pending(&info->receiver->work.dwork));
    zassert_equal(atomic_get(&on_end_calls), 0);

    terminal_notification(conn, params);
    flush_cleanup(device);
    zassert_equal(atomic_get(&on_end_calls), 1);
}
