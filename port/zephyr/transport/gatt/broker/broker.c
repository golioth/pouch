/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "broker.h"
#include "discover.h"
#include "../common.h"
#include "transport/sar/receiver.h"
#include "transport/sar/sender.h"
#include "transport/endpoints/broker/endpoints.h"

#include <string.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(broker, CONFIG_POUCH_GATEWAY_GATT_LOG_LEVEL);

#define RECEIVER(_endpoint)                    \
    {                                          \
        .type = CHAR_RECEIVER,                 \
        .receiver = &((struct pouch_receiver){ \
            .endpoint = (_endpoint),           \
        }),                                    \
    }
#define SENDER(_endpoint)                  \
    {                                      \
        .type = CHAR_SENDER,               \
        .sender = &((struct pouch_sender){ \
            .endpoint = (_endpoint),       \
        }),                                \
    }

static struct broker_bt_gatt_device devices[CONFIG_BT_MAX_CONN] = {
    [0 ...(CONFIG_BT_MAX_CONN - 1)] =
        {
            .chars =
                {
                    [BROKER_BT_ATTR_INFO] = RECEIVER(&broker_endpoint_info),
                    [BROKER_BT_ATTR_DEVICE_CERT] = RECEIVER(&broker_endpoint_device_cert),
                    [BROKER_BT_ATTR_SERVER_CERT] = SENDER(&broker_endpoint_server_cert),
                    [BROKER_BT_ATTR_UPLINK] = RECEIVER(&broker_endpoint_uplink),
                    [BROKER_BT_ATTR_DOWNLINK] = SENDER(&broker_endpoint_downlink),
                },
        },
};

static void discover_complete(struct broker_bt_gatt_device *device, bool success);
static void sub_complete(struct broker_bt_gatt_device *device, enum broker_bt_attr attr);
static void link_complete(struct broker_bt_gatt_device *device, enum broker_bt_attr attr);
static void cleanup_handler(struct k_work *work);
static bool cleanup_drained(struct broker_bt_gatt_device *device);
static void finalize(struct broker_bt_gatt_device *device);

static bool session_is(struct broker_bt_gatt_device *device, enum broker_session_state state)
{
    return atomic_get(&device->state) == state;
}

static bool characteristic_is(struct characteristic *c, enum broker_characteristic_flag flag)
{
    return atomic_test_bit(&c->flags, flag);
}

/*
 * This broker is pinned to Zephyr's GATT client implementation, which sets its internal
 * WRITE_PENDING flag before queuing a CCC write and clears it before invoking notify/subscribe
 * completion callbacks. The flag is used here only to keep sub_params alive and to reconcile the
 * known unsubscribe path where Zephyr removes the subscription before a final callback can run.
 */
static bool ccc_write_pending(struct characteristic *c)
{
    return atomic_test_bit(c->sub_params.flags, BT_GATT_SUBSCRIBE_FLAG_WRITE_PENDING);
}

static void schedule_cleanup(struct broker_bt_gatt_device *device)
{
    if (!atomic_test_bit(&device->flags, BROKER_DEVICE_CLEANUP_CALL_ACTIVE))
    {
        (void) k_work_schedule(&device->cleanup_work, K_NO_WAIT);
    }
}

static void request_auto_unsubscribe(struct broker_bt_gatt_device *device, struct characteristic *c)
{
    if (!atomic_test_and_set_bit(&c->flags, BROKER_CHAR_UNSUBSCRIBE_PENDING))
    {
        atomic_set_bit(&c->flags, BROKER_CHAR_AUTO_UNSUBSCRIBE_PENDING);
    }

    (void) k_work_reschedule(&device->cleanup_work, K_MSEC(1));
}

static void reset_session(struct broker_bt_gatt_device *device)
{
    memset(&device->node, 0, sizeof(device->node));
    memset(&device->discover, 0, sizeof(device->discover));
    device->conn = NULL;
    device->callback = NULL;
    atomic_set(&device->flags, 0);

    for (enum broker_bt_attr i = 0; i < BROKER_BT_ATTRS; i++)
    {
        struct characteristic *c = &device->chars[i];

        memset(&c->bearer, 0, sizeof(c->bearer));
        memset(&c->sub_params, 0, sizeof(c->sub_params));
        memset(&c->handle, 0, sizeof(c->handle));
        c->callback = NULL;
        atomic_set(&c->flags, 0);
    }
}

static void broker_locks_init(void)
{
    for (size_t i = 0; i < CONFIG_BT_MAX_CONN; i++)
    {
        pouch_mutex_init(&devices[i].lock);
    }
}
POUCH_APPLICATION_STARTUP_HOOK(broker_locks_init);

static inline struct broker_bt_gatt_device *device_from_characteristic(struct characteristic *c)
{
    ptrdiff_t diff = (intptr_t) c - (intptr_t) &devices[0];
    size_t index = diff / sizeof(devices[0]);  // rounds down
    if (index < CONFIG_BT_MAX_CONN)
    {
        return &devices[index];
    }

    return NULL;
}

struct broker_bt_gatt_device *broker_bt_gatt_device(struct bt_conn *conn)
{
    return &devices[bt_conn_index(conn)];
}

bool broker_bt_discovery_active(struct broker_bt_gatt_device *device, struct bt_conn *conn)
{
    return device->conn == conn && atomic_get(&device->state) == BROKER_SESSION_DISCOVERING;
}

static void finish(struct broker_bt_gatt_device *device, bool notify_end)
{
    atomic_val_t state;

    if (!notify_end)
    {
        atomic_clear_bit(&device->flags, BROKER_DEVICE_NOTIFY_END);
    }

    state = atomic_get(&device->state);
    while (state == BROKER_SESSION_DISCOVERING || state == BROKER_SESSION_RUNNING)
    {
        if (atomic_cas(&device->state, state, BROKER_SESSION_CLOSING))
        {
            schedule_cleanup(device);
            return;
        }

        state = atomic_get(&device->state);
    }
}

static int bearer_send(struct pouch_bearer *bearer, const uint8_t *buf, size_t len)
{
    struct characteristic *c = CONTAINER_OF(bearer, struct characteristic, bearer);
    struct broker_bt_gatt_device *device = device_from_characteristic(c);
    if (device == NULL || device->conn == NULL || !session_is(device, BROKER_SESSION_RUNNING)
        || !characteristic_is(c, BROKER_CHAR_BEARER_OPEN))
    {
        return -ENOTCONN;
    }

    return bt_gatt_write_without_response(device->conn, c->handle.value, buf, len, false);
}

static void bearer_close(struct pouch_bearer *bearer, bool success)
{
    struct characteristic *c = CONTAINER_OF(bearer, struct characteristic, bearer);
    struct broker_bt_gatt_device *device = device_from_characteristic(c);
    if (device == NULL)
    {
        LOG_ERR("Unknown bearer %p", bearer);
        return;
    }

    if (!atomic_test_and_clear_bit(&c->flags, BROKER_CHAR_BEARER_OPEN))
    {
        return;
    }

    LOG_DBG("%p: %s", c, success ? "success" : "fail");
    if (success)
    {
        atomic_set_bit(&c->flags, BROKER_CHAR_TRANSFER_SUCCESS);
    }
    else
    {
        finish(device, true);
    }
}

static void bearer_ready(struct pouch_bearer *bearer)
{
    struct characteristic *c = CONTAINER_OF(bearer, struct characteristic, bearer);
    struct broker_bt_gatt_device *device = device_from_characteristic(c);

    if (device == NULL || device->conn == NULL || !session_is(device, BROKER_SESSION_RUNNING)
        || !characteristic_is(c, BROKER_CHAR_BEARER_OPEN))
    {
        return;
    }

    pouch_mutex_lock(&device->lock, POUCH_FOREVER);
    switch (c->type)
    {
        case CHAR_RECEIVER:
            pouch_receiver_ready(c->receiver);
            break;
        case CHAR_SENDER:
            pouch_sender_ready(c->sender);
            break;
    }
    pouch_mutex_unlock(&device->lock);
}

static int open(struct characteristic *c)
{
    struct broker_bt_gatt_device *device = device_from_characteristic(c);
    int ret = -EINVAL;

    if (device == NULL)
    {
        return -ENOENT;
    }

    pouch_mutex_lock(&device->lock, POUCH_FOREVER);
    switch (c->type)
    {
        case CHAR_RECEIVER:
            ret = pouch_receiver_open(c->receiver, &c->bearer, CONFIG_POUCH_GATT_WINDOW_SIZE);
            break;
        case CHAR_SENDER:
            ret = pouch_sender_open(c->sender, &c->bearer);
            break;
    }
    pouch_mutex_unlock(&device->lock);
    return ret;
}

static void close(struct characteristic *c)
{
    struct broker_bt_gatt_device *device = device_from_characteristic(c);

    if (device == NULL)
    {
        return;
    }

    /* This must be a recursive lock to avoid a deadlock in a case where the recv() function has
     * taken the lock, then sar.c calls end() which eventually calls this close() function
     */
    pouch_mutex_lock(&device->lock, POUCH_FOREVER);
    switch (c->type)
    {
        case CHAR_RECEIVER:
            pouch_receiver_close(c->receiver);
            break;
        case CHAR_SENDER:
            pouch_sender_close(c->sender);
            break;
    }
    pouch_mutex_unlock(&device->lock);
}

static void ccc_rsp_cb(struct bt_conn *conn, uint8_t err, struct bt_gatt_subscribe_params *params)
{
    struct broker_bt_gatt_device *device = broker_bt_gatt_device(conn);
    struct characteristic *c = CONTAINER_OF(params, struct characteristic, sub_params);
    bool initial_write;
    bool final_write;

    if (device->conn != conn)
    {
        return;
    }

    /* Cancellation resolves the initial write before the unsubscribe write is queued. */
    initial_write = atomic_test_and_clear_bit(&c->flags, BROKER_CHAR_CCC_SUBSCRIBE_PENDING);
    final_write = !initial_write && characteristic_is(c, BROKER_CHAR_UNSUBSCRIBE_PENDING);
    if (!initial_write)
    {
        atomic_clear_bit(&c->flags, BROKER_CHAR_CCC_UNSUBSCRIBE_PENDING);
        atomic_clear_bit(&c->flags, BROKER_CHAR_CCC_RECONCILE_PENDING);
        atomic_clear_bit(&c->flags, BROKER_CHAR_AUTO_UNSUBSCRIBE_PENDING);
        atomic_clear_bit(&c->flags, BROKER_CHAR_UNSUBSCRIBE_PENDING);
    }

    if (err)
    {
        if (final_write)
        {
            atomic_clear_bit(&c->flags, BROKER_CHAR_TRANSFER_SUCCESS);
        }

        if (!characteristic_is(c, BROKER_CHAR_GATT_TERMINATED))
        {
            atomic_clear_bit(&c->flags, BROKER_CHAR_GATT_ACTIVE);
            atomic_set_bit(&c->flags, BROKER_CHAR_GATT_TERMINATED);
            close(c);
        }

        if (final_write)
        {
            finish(device, true);
        }
    }

    schedule_cleanup(device);
}

static int recv(struct characteristic *c, const void *data, size_t length)
{
    struct broker_bt_gatt_device *device = device_from_characteristic(c);
    int ret = -EINVAL;

    if (device == NULL)
    {
        return -ENOENT;
    }

    pouch_mutex_lock(&device->lock, POUCH_FOREVER);
    switch (c->type)
    {
        case CHAR_RECEIVER:
            ret = pouch_receiver_recv(c->receiver, data, length);
            break;
        case CHAR_SENDER:
            ret = pouch_sender_recv(c->sender, data, length);
            break;
    }
    pouch_mutex_unlock(&device->lock);
    return ret;
}

static uint8_t notify_cb(struct bt_conn *conn,
                         struct bt_gatt_subscribe_params *params,
                         const void *data,
                         uint16_t length)
{
    struct characteristic *c = CONTAINER_OF(params, struct characteristic, sub_params);
    struct broker_bt_gatt_device *device = device_from_characteristic(c);

    if (device == NULL || (conn != NULL && device->conn != conn))
    {
        return BT_GATT_ITER_STOP;
    }

    if (NULL == data)
    {
        LOG_DBG("Subscription terminated");
        atomic_clear_bit(&c->flags, BROKER_CHAR_GATT_ACTIVE);
        atomic_set_bit(&c->flags, BROKER_CHAR_GATT_TERMINATED);

        if (conn == NULL)
        {
            atomic_set_bit(&device->flags, BROKER_DEVICE_DISCONNECTED);
            finish(device, false);
            schedule_cleanup(device);
            return BT_GATT_ITER_STOP;
        }

        close(c);
        schedule_cleanup(device);

        return BT_GATT_ITER_STOP;
    }

    if (!session_is(device, BROKER_SESSION_RUNNING)
        || !characteristic_is(c, BROKER_CHAR_BEARER_OPEN))
    {
        request_auto_unsubscribe(device, c);
        return BT_GATT_ITER_STOP;
    }

    int err = recv(c, data, length);
    if (err)
    {
        LOG_ERR("Recv failed: %d", err);
        request_auto_unsubscribe(device, c);
        finish(device, true);
        return BT_GATT_ITER_STOP;
    }

    if (!characteristic_is(c, BROKER_CHAR_BEARER_OPEN))
    {
        LOG_DBG("Unsubscribing");
        request_auto_unsubscribe(device, c);
        return BT_GATT_ITER_STOP;
    }

    return BT_GATT_ITER_CONTINUE;
}

static int subscribe(struct broker_bt_gatt_device *device,
                     enum broker_bt_attr attr,
                     broker_bt_gatt_char_done_t callback)
{
    uint16_t mtu = bt_gatt_get_mtu(device->conn);
    if (mtu <= BT_ATT_OVERHEAD)
    {
        return -EIO;
    }

    struct characteristic *c = &device->chars[attr];

    c->bearer.close = bearer_close;
    c->bearer.ready = bearer_ready;
    c->bearer.send = bearer_send;
    c->bearer.ctx = &device->node;
    c->bearer.maxlen = mtu - BT_ATT_OVERHEAD;

    struct bt_gatt_subscribe_params params = {
        .notify = notify_cb,
        .subscribe = ccc_rsp_cb,
        .value = BT_GATT_CCC_NOTIFY,
        .value_handle = c->handle.value,
        .ccc_handle = c->handle.ccc,
    };
    atomic_set_bit(params.flags, BT_GATT_SUBSCRIBE_FLAG_VOLATILE);

    c->sub_params = params;
    c->callback = callback;
    atomic_set_bit(&c->flags, BROKER_CHAR_CCC_SUBSCRIBE_PENDING);

    int err = bt_gatt_subscribe(device->conn, &c->sub_params);
    if (err)
    {
        atomic_clear_bit(&c->flags, BROKER_CHAR_CCC_SUBSCRIBE_PENDING);
        return err;
    }
    if (!ccc_write_pending(c))
    {
        atomic_clear_bit(&c->flags, BROKER_CHAR_CCC_SUBSCRIBE_PENDING);
    }
    atomic_set_bit(&c->flags, BROKER_CHAR_GATT_ACTIVE);

    atomic_set_bit(&c->flags, BROKER_CHAR_BEARER_OPEN);
    err = open(c);
    if (err)
    {
        LOG_ERR("Failed to open bearer");
        atomic_clear_bit(&c->flags, BROKER_CHAR_BEARER_OPEN);
        return err;
    }

    return 0;
}

static void complete_characteristics(struct broker_bt_gatt_device *device)
{
    for (enum broker_bt_attr i = 0; i < BROKER_BT_ATTRS; i++)
    {
        struct characteristic *c = &device->chars[i];

        if (!characteristic_is(c, BROKER_CHAR_GATT_TERMINATED)
            || characteristic_is(c, BROKER_CHAR_CCC_SUBSCRIBE_PENDING)
            || characteristic_is(c, BROKER_CHAR_CCC_UNSUBSCRIBE_PENDING)
            || characteristic_is(c, BROKER_CHAR_CCC_RECONCILE_PENDING)
            || characteristic_is(c, BROKER_CHAR_AUTO_UNSUBSCRIBE_PENDING)
            || characteristic_is(c, BROKER_CHAR_UNSUBSCRIBE_PENDING))
        {
            continue;
        }

        if (!atomic_test_and_clear_bit(&c->flags, BROKER_CHAR_GATT_TERMINATED))
        {
            continue;
        }

        bool success = atomic_test_and_clear_bit(&c->flags, BROKER_CHAR_TRANSFER_SUCCESS);
        if (session_is(device, BROKER_SESSION_RUNNING))
        {
            if (success && c->callback != NULL)
            {
                c->callback(device, i);
            }
            else
            {
                finish(device, true);
            }
        }
    }
}

static bool cleanup_drained(struct broker_bt_gatt_device *device)
{
    if (atomic_test_bit(&device->flags, BROKER_DEVICE_DISCOVER_PENDING))
    {
        return false;
    }

    for (enum broker_bt_attr i = 0; i < BROKER_BT_ATTRS; i++)
    {
        struct characteristic *c = &device->chars[i];

        if (characteristic_is(c, BROKER_CHAR_GATT_ACTIVE)
            || characteristic_is(c, BROKER_CHAR_UNSUBSCRIBE_PENDING)
            || characteristic_is(c, BROKER_CHAR_AUTO_UNSUBSCRIBE_PENDING)
            || characteristic_is(c, BROKER_CHAR_CCC_SUBSCRIBE_PENDING)
            || characteristic_is(c, BROKER_CHAR_CCC_UNSUBSCRIBE_PENDING)
            || characteristic_is(c, BROKER_CHAR_CCC_RECONCILE_PENDING)
            || characteristic_is(c, BROKER_CHAR_GATT_TERMINATED))
        {
            return false;
        }
    }

    return true;
}

static void finalize(struct broker_bt_gatt_device *device)
{
    if (!session_is(device, BROKER_SESSION_CLOSING) || !cleanup_drained(device))
    {
        return;
    }

    struct bt_conn *conn = device->conn;
    pouch_gateway_bt_end_t callback = device->callback;
    bool notify_end = atomic_test_bit(&device->flags, BROKER_DEVICE_NOTIFY_END)
        && !atomic_test_bit(&device->flags, BROKER_DEVICE_DISCONNECTED);

    if (notify_end && callback != NULL && conn != NULL)
    {
        callback(conn);
    }

    device->callback = NULL;
    device->conn = NULL;
    atomic_set(&device->flags, 0);
    atomic_set(&device->state, BROKER_SESSION_IDLE);

    if (conn != NULL)
    {
        bt_conn_unref(conn);
    }
}

static void cleanup_handler(struct k_work *work)
{
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct broker_bt_gatt_device *device =
        CONTAINER_OF(dwork, struct broker_bt_gatt_device, cleanup_work);
    bool closing;
    bool retry = false;

    if (!session_is(device, BROKER_SESSION_RUNNING) && !session_is(device, BROKER_SESSION_CLOSING))
    {
        return;
    }

    complete_characteristics(device);
    closing = session_is(device, BROKER_SESSION_CLOSING);

    if (closing)
    {
        for (enum broker_bt_attr i = 0; i < BROKER_BT_ATTRS; i++)
        {
            struct characteristic *c = &device->chars[i];

            if (atomic_test_and_clear_bit(&c->flags, BROKER_CHAR_BEARER_OPEN))
            {
                close(c);
            }
        }

        if (atomic_test_bit(&device->flags, BROKER_DEVICE_DISCOVER_PENDING)
            && !atomic_test_and_set_bit(&device->flags, BROKER_DEVICE_DISCOVER_CANCEL_REQUESTED))
        {
            atomic_set_bit(&device->flags, BROKER_DEVICE_CLEANUP_CALL_ACTIVE);
            bt_gatt_cancel(device->conn, &device->discover.params);
            atomic_clear_bit(&device->flags, BROKER_DEVICE_CLEANUP_CALL_ACTIVE);
        }
    }

    for (enum broker_bt_attr i = 0; i < BROKER_BT_ATTRS; i++)
    {
        struct characteristic *c = &device->chars[i];
        bool write_pending;

        if (closing && characteristic_is(c, BROKER_CHAR_GATT_ACTIVE)
            && !characteristic_is(c, BROKER_CHAR_UNSUBSCRIBE_PENDING))
        {
            atomic_set_bit(&c->flags, BROKER_CHAR_UNSUBSCRIBE_PENDING);
        }

        if (!characteristic_is(c, BROKER_CHAR_UNSUBSCRIBE_PENDING))
        {
            continue;
        }

        write_pending = ccc_write_pending(c);

        if (atomic_test_and_clear_bit(&c->flags, BROKER_CHAR_AUTO_UNSUBSCRIBE_PENDING))
        {
            if (write_pending && !characteristic_is(c, BROKER_CHAR_CCC_SUBSCRIBE_PENDING))
            {
                atomic_set_bit(&c->flags, BROKER_CHAR_CCC_UNSUBSCRIBE_PENDING);
            }
            else if (characteristic_is(c, BROKER_CHAR_GATT_ACTIVE))
            {
                retry = true;
                continue;
            }
        }

        if (characteristic_is(c, BROKER_CHAR_CCC_UNSUBSCRIBE_PENDING))
        {
            if (write_pending)
            {
                atomic_clear_bit(&c->flags, BROKER_CHAR_CCC_RECONCILE_PENDING);
                retry = true;
                continue;
            }

            if (!atomic_test_and_set_bit(&c->flags, BROKER_CHAR_CCC_RECONCILE_PENDING))
            {
                retry = true;
                continue;
            }

            atomic_clear_bit(&c->flags, BROKER_CHAR_CCC_RECONCILE_PENDING);
            atomic_clear_bit(&c->flags, BROKER_CHAR_CCC_UNSUBSCRIBE_PENDING);
            atomic_clear_bit(&c->flags, BROKER_CHAR_UNSUBSCRIBE_PENDING);
            atomic_clear_bit(&c->flags, BROKER_CHAR_GATT_ACTIVE);
            atomic_clear_bit(&c->flags, BROKER_CHAR_TRANSFER_SUCCESS);
            atomic_set_bit(&c->flags, BROKER_CHAR_GATT_TERMINATED);
            close(c);
            finish(device, true);
            continue;
        }

        if (!characteristic_is(c, BROKER_CHAR_GATT_ACTIVE))
        {
            atomic_clear_bit(&c->flags, BROKER_CHAR_CCC_RECONCILE_PENDING);
            atomic_clear_bit(&c->flags, BROKER_CHAR_UNSUBSCRIBE_PENDING);
            continue;
        }

        if (write_pending)
        {
            if (!characteristic_is(c, BROKER_CHAR_CCC_SUBSCRIBE_PENDING))
            {
                atomic_set_bit(&c->flags, BROKER_CHAR_CCC_UNSUBSCRIBE_PENDING);
            }
            retry = true;
            continue;
        }

        atomic_set_bit(&c->flags, BROKER_CHAR_CCC_UNSUBSCRIBE_PENDING);
        atomic_set_bit(&device->flags, BROKER_DEVICE_CLEANUP_CALL_ACTIVE);
        int err = bt_gatt_unsubscribe(device->conn, &c->sub_params);
        atomic_clear_bit(&device->flags, BROKER_DEVICE_CLEANUP_CALL_ACTIVE);
        if (err || !ccc_write_pending(c))
        {
            atomic_clear_bit(&c->flags, BROKER_CHAR_CCC_UNSUBSCRIBE_PENDING);
        }
        if (err == -EINVAL || err == -ENOTCONN)
        {
            atomic_clear_bit(&c->flags, BROKER_CHAR_GATT_ACTIVE);
            atomic_clear_bit(&c->flags, BROKER_CHAR_UNSUBSCRIBE_PENDING);
            atomic_clear_bit(&c->flags, BROKER_CHAR_TRANSFER_SUCCESS);
            atomic_set_bit(&c->flags, BROKER_CHAR_GATT_TERMINATED);
            close(c);
            if (err == -ENOTCONN)
            {
                finish(device, false);
            }
            else
            {
                finish(device, true);
            }
        }
        else if (err)
        {
            retry = true;
        }
        else if (!ccc_write_pending(c))
        {
            atomic_clear_bit(&c->flags, BROKER_CHAR_UNSUBSCRIBE_PENDING);
        }
        else
        {
            retry = true;
        }
    }

    complete_characteristics(device);
    if (!closing && session_is(device, BROKER_SESSION_CLOSING))
    {
        retry = true;
    }

    if (retry)
    {
        (void) k_work_reschedule(&device->cleanup_work, K_MSEC(1));
        return;
    }

    finalize(device);
}

void broker_bt_disconnected(struct bt_conn *conn, uint8_t reason)
{
    struct broker_bt_gatt_device *device = broker_bt_gatt_device(conn);

    ARG_UNUSED(reason);

    if (device->conn != conn)
    {
        return;
    }

    atomic_set_bit(&device->flags, BROKER_DEVICE_DISCONNECTED);
    finish(device, false);
}

BT_CONN_CB_DEFINE(gateway_conn_listener) = {
    .disconnected = broker_bt_disconnected,
};

int pouch_gateway_bt_start(struct bt_conn *conn, pouch_gateway_bt_end_t callback)
{
    struct bt_conn *owned_conn;
    struct broker_bt_gatt_device *device;
    int err;

    if (conn == NULL || callback == NULL)
    {
        return -EINVAL;
    }

    owned_conn = bt_conn_ref(conn);
    if (owned_conn == NULL)
    {
        return -ENOTCONN;
    }

    device = broker_bt_gatt_device(owned_conn);
    if (!atomic_cas(&device->state, BROKER_SESSION_IDLE, BROKER_SESSION_DISCOVERING))
    {
        bt_conn_unref(owned_conn);
        return -EBUSY;
    }

    if (!device->cleanup_work_initialized)
    {
        k_work_init_delayable(&device->cleanup_work, cleanup_handler);
        device->cleanup_work_initialized = true;
    }

    reset_session(device);
    device->conn = owned_conn;
    device->callback = callback;
    atomic_set_bit(&device->flags, BROKER_DEVICE_NOTIFY_END);
    atomic_set_bit(&device->flags, BROKER_DEVICE_DISCOVER_PENDING);

    // TODO: Could skip this if we've already ran discovery on this device:
    err = gateway_bt_discover(device, discover_complete);
    if (err)
    {
        LOG_ERR("device %d: ERR: %d", device - &devices[0], err);

        memset(&device->discover, 0, sizeof(device->discover));
        device->callback = NULL;
        device->conn = NULL;
        atomic_set(&device->flags, 0);
        atomic_set(&device->state, BROKER_SESSION_IDLE);
        bt_conn_unref(owned_conn);
        return err;
    }

    return 0;
}

static void discover_complete(struct broker_bt_gatt_device *device, bool success)
{
    atomic_clear_bit(&device->flags, BROKER_DEVICE_DISCOVER_PENDING);

    if (session_is(device, BROKER_SESSION_CLOSING))
    {
        schedule_cleanup(device);
        return;
    }

    if (!session_is(device, BROKER_SESSION_DISCOVERING))
    {
        return;
    }

    if (!success)
    {
        finish(device, true);
        return;
    }

    if (!atomic_cas(&device->state, BROKER_SESSION_DISCOVERING, BROKER_SESSION_RUNNING))
    {
        if (session_is(device, BROKER_SESSION_CLOSING))
        {
            schedule_cleanup(device);
        }
        return;
    }

    // Start by reading the info endpoint:
    int err = subscribe(device, BROKER_BT_ATTR_INFO, sub_complete);
    if (err)
    {
        finish(device, true);
        return;
    }
}

static void sub_complete(struct broker_bt_gatt_device *device, enum broker_bt_attr attr)
{
    int err;

    if (!session_is(device, BROKER_SESSION_RUNNING))
    {
        return;
    }

    if (!device->node.server_cert_provisioned)
    {
        if (attr == BROKER_BT_ATTR_SERVER_CERT)
        {
            LOG_ERR("Failed to provision server cert");
            finish(device, true);
            return;
        }

        err = subscribe(device, BROKER_BT_ATTR_SERVER_CERT, sub_complete);
        if (err)
        {
            finish(device, true);
            return;
        }

        return;
    }

    if (!device->node.device_cert_provisioned)
    {
        if (attr == BROKER_BT_ATTR_DEVICE_CERT)
        {
            LOG_ERR("Failed to provision device cert");
            finish(device, true);
            return;
        }

        err = subscribe(device, BROKER_BT_ATTR_DEVICE_CERT, sub_complete);
        if (err)
        {
            finish(device, true);
            return;
        }

        return;
    }

    // start link:
    err = subscribe(device, BROKER_BT_ATTR_DOWNLINK, link_complete);
    if (err)
    {
        finish(device, true);
        return;
    }
    err = subscribe(device, BROKER_BT_ATTR_UPLINK, link_complete);
    if (err)
    {
        finish(device, true);
        return;
    }
}

static void link_complete(struct broker_bt_gatt_device *device, enum broker_bt_attr attr)
{
    ARG_UNUSED(attr);

    if (!session_is(device, BROKER_SESSION_RUNNING))
    {
        return;
    }

    if (characteristic_is(&device->chars[BROKER_BT_ATTR_DOWNLINK], BROKER_CHAR_GATT_ACTIVE)
        || characteristic_is(&device->chars[BROKER_BT_ATTR_UPLINK], BROKER_CHAR_GATT_ACTIVE))
    {
        // wait for both uplink and downlink to finish
        return;
    }

    finish(device, true);
}
