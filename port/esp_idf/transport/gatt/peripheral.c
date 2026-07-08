/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <errno.h>
#include <stdbool.h>

#include "esp_log.h"
#include "host/ble_hs.h"
#include "services/gatt/ble_svc_gatt.h"

#include <pouch/port.h>

#include "common.h"
#include "transport/sar/receiver.h"
#include "transport/sar/sender.h"
#include "transport/endpoints/device/endpoints.h"

static const char *TAG = "pouch_gatt";

static uint16_t _pouch_mtu = BLE_ATT_MTU_DFLT;
static uint16_t _conn_handle = BLE_HS_CONN_HANDLE_NONE;

/***************************************************
 * Init macros
 **************************************************/

/* Forward declarations */
static int bearer_send(struct pouch_bearer *bearer, const uint8_t *buf, size_t len);
static void bearer_close(struct pouch_bearer *bearer, bool success);
static void bearer_ready(struct pouch_bearer *bearer);

#define CHAR_INIT_RECEIVER(_endpoint)                                    \
    {                                                                    \
        .bearer =                                                        \
            {                                                            \
                .send = bearer_send,                                     \
                .close = bearer_close,                                   \
                .ready = bearer_ready,                                   \
            },                                                           \
        .type = CHAR_RECEIVER,                                           \
        .receiver = &((struct pouch_receiver){.endpoint = (_endpoint)}), \
    }

#define CHAR_INIT_SENDER(_endpoint)                                  \
    {                                                                \
        .bearer =                                                    \
            {                                                        \
                .send = bearer_send,                                 \
                .close = bearer_close,                               \
                .ready = bearer_ready,                               \
            },                                                       \
        .type = CHAR_SENDER,                                         \
        .sender = &((struct pouch_sender){.endpoint = (_endpoint)}), \
    }

#define DEFINE_GATT_CHAR(_char_ptr, _uuid_ptr, _cb, _flags) \
    {                                                       \
        .uuid = _uuid_ptr,                                  \
        .access_cb = _cb,                                   \
        .arg = (_char_ptr),                                 \
        .val_handle = &((_char_ptr)->val_handle),           \
        .flags = _flags,                                    \
    }

/***************************************************
 * Types
 **************************************************/

enum characteristic_type
{
    CHAR_RECEIVER,
    CHAR_SENDER,
};

struct pouch_characteristic
{
    struct pouch_bearer bearer;

    /* Session context */
    bool subscribed;
    uint16_t val_handle;

    /* Higher level handler */
    enum characteristic_type type;
    union
    {
        struct pouch_sender *sender;
        struct pouch_receiver *receiver;
    };
};

/***************************************************
 * Bearer callbacks
 **************************************************/

static int bearer_send(struct pouch_bearer *bearer, const uint8_t *buf, size_t len)
{
    struct pouch_characteristic *c = CONTAINER_OF(bearer, struct pouch_characteristic, bearer);

    struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, len);
    if (NULL == om)
    {
        ESP_LOGE(TAG, "Failed to allocate mbuf for notification");
        return -ENOMEM;
    }

    int rc = ble_gatts_notify_custom(_conn_handle, c->val_handle, om);
    if (0 != rc)
    {
        ESP_LOGE(TAG, "Failed to send notification: %d", rc);
        return -EIO;
    }

    return 0;
}

static void bearer_close(struct pouch_bearer *bearer, bool success)
{
    struct pouch_characteristic *c = CONTAINER_OF(bearer, struct pouch_characteristic, bearer);

    if (!success)
    {
        ESP_LOGD(TAG, "%p: close", c);
        ble_gap_terminate(_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
}

static void bearer_ready(struct pouch_bearer *bearer)
{
    struct pouch_characteristic *c = CONTAINER_OF(bearer, struct pouch_characteristic, bearer);

    ESP_LOGD(TAG, "%p: ready", c);
    switch (c->type)
    {
        case CHAR_RECEIVER:
            return pouch_receiver_ready(c->receiver);
        case CHAR_SENDER:
            return pouch_sender_ready(c->sender);
    }
}

/***************************************************
 * Helpers
 **************************************************/

static int recv(const struct pouch_characteristic *c, const void *buf, size_t len)
{
    switch (c->type)
    {
        case CHAR_RECEIVER:
            return pouch_receiver_recv(c->receiver, buf, len);
        case CHAR_SENDER:
            return pouch_sender_recv(c->sender, buf, len);
    }

    return -EINVAL;
}

static int data_write(uint16_t conn_handle,
                      uint16_t attr_handle,
                      struct ble_gatt_access_ctxt *ctxt,
                      void *arg)
{
    struct pouch_characteristic *c = (struct pouch_characteristic *) arg;

    switch (ctxt->op)
    {
        case BLE_GATT_ACCESS_OP_WRITE_CHR:
        {
            uint16_t om_len = OS_MBUF_PKTLEN(ctxt->om);
            uint8_t buf[CONFIG_POUCH_TRANSPORT_GATT_MTU_SIZE - BT_ATT_OVERHEAD];

            if (om_len > sizeof(buf))
            {
                return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
            }

            int rc = ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof(buf), &om_len);
            if (0 != rc)
            {
                return BLE_ATT_ERR_UNLIKELY;
            }

            int err = recv(c, buf, om_len);
            if (err)
            {
                ESP_LOGD(TAG, "recv failed: %d", err);
            }
            return 0;
        }

        default:
            return BLE_ATT_ERR_UNLIKELY;
    }
}

static int open(struct pouch_characteristic *c)
{
    switch (c->type)
    {
        case CHAR_RECEIVER:
            return pouch_receiver_open(c->receiver,
                                       &c->bearer,
                                       CONFIG_POUCH_TRANSPORT_GATT_WINDOW_SIZE);
        case CHAR_SENDER:
            return pouch_sender_open(c->sender, &c->bearer);
    }

    return -ENOTSUP;
}

static int close(struct pouch_characteristic *c)
{
    switch (c->type)
    {
        case CHAR_RECEIVER:
            pouch_receiver_close(c->receiver);
            return 0;
        case CHAR_SENDER:
            pouch_sender_close(c->sender);
            return 0;
    }

    return -ENOTSUP;
}

/***************************************************
 * Pouch characteristic contexts
 **************************************************/

enum pouch_characteristic_type
{
    POUCH_CHAR_DOWNLINK,
    POUCH_CHAR_UPLINK,
    POUCH_CHAR_INFO,
#if CONFIG_POUCH_ENCRYPTION_SAEAD
    POUCH_CHAR_SERVER_CERT,
    POUCH_CHAR_DEVICE_CERT,
#endif
    POUCH_CHAR_COUNT
};

static struct pouch_characteristic all_chars[POUCH_CHAR_COUNT] = {
    [POUCH_CHAR_DOWNLINK] = CHAR_INIT_RECEIVER(&pouch_device_endpoint_downlink),
    [POUCH_CHAR_UPLINK] = CHAR_INIT_SENDER(&pouch_device_endpoint_uplink),
    [POUCH_CHAR_INFO] = CHAR_INIT_SENDER(&pouch_device_endpoint_info),
#if CONFIG_POUCH_ENCRYPTION_SAEAD
    [POUCH_CHAR_SERVER_CERT] = CHAR_INIT_RECEIVER(&pouch_device_endpoint_server_cert),
    [POUCH_CHAR_DEVICE_CERT] = CHAR_INIT_SENDER(&pouch_device_endpoint_device_cert),
#endif
};

/***************************************************
 * Handle lookup
 **************************************************/

static struct pouch_characteristic *char_find_by_val_handle(uint16_t val_handle)
{
    for (size_t i = 0; i < POUCH_CHAR_COUNT; i++)
    {
        if (all_chars[i].val_handle == val_handle)
        {
            return &all_chars[i];
        }
    }

    return NULL;
}

/***************************************************
 * GATT Service Definition (NimBLE)
 **************************************************/

static const ble_uuid16_t pouch_svc_uuid = POUCH_GATT_UUID_SVC_VAL;
static const ble_uuid128_t pouch_downlink_uuid = POUCH_GATT_UUID_DOWNLINK_CHRC_VAL;
static const ble_uuid128_t pouch_uplink_uuid = POUCH_GATT_UUID_UPLINK_CHRC_VAL;
static const ble_uuid128_t pouch_info_uuid = POUCH_GATT_UUID_INFO_CHRC_VAL;

#if CONFIG_POUCH_ENCRYPTION_SAEAD
static const ble_uuid128_t pouch_server_cert_uuid = POUCH_GATT_UUID_SERVER_CERT_CHRC_VAL;
static const ble_uuid128_t pouch_device_cert_uuid = POUCH_GATT_UUID_DEVICE_CERT_CHRC_VAL;
#endif

#define CHR_FLAGS (BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_WRITE_ENC)

static const struct ble_gatt_svc_def pouch_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &pouch_svc_uuid.u,
        .characteristics =
            (struct ble_gatt_chr_def[]){
                DEFINE_GATT_CHAR(&all_chars[POUCH_CHAR_DOWNLINK],
                                 &pouch_downlink_uuid.u,
                                 data_write,
                                 CHR_FLAGS),
                DEFINE_GATT_CHAR(&all_chars[POUCH_CHAR_UPLINK],
                                 &pouch_uplink_uuid.u,
                                 data_write,
                                 CHR_FLAGS),
                DEFINE_GATT_CHAR(&all_chars[POUCH_CHAR_INFO],
                                 &pouch_info_uuid.u,
                                 data_write,
                                 CHR_FLAGS),
#if CONFIG_POUCH_ENCRYPTION_SAEAD
                DEFINE_GATT_CHAR(&all_chars[POUCH_CHAR_SERVER_CERT],
                                 &pouch_server_cert_uuid.u,
                                 data_write,
                                 CHR_FLAGS),
                DEFINE_GATT_CHAR(&all_chars[POUCH_CHAR_DEVICE_CERT],
                                 &pouch_device_cert_uuid.u,
                                 data_write,
                                 CHR_FLAGS),
#endif
                {0}, /* Terminator */
            },
    },
    {0}, /* Terminator */
};

/***************************************************
 * GAP event handler
 **************************************************/

int pouch_gatt_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type)
    {
        case BLE_GAP_EVENT_CONNECT:
            if (0 == event->connect.status)
            {
                ESP_LOGI(TAG, "Client connected, conn_handle=%u", event->connect.conn_handle);
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "Client disconnected, reason=%d", event->disconnect.reason);

            _pouch_mtu = BLE_ATT_MTU_DFLT;
            _conn_handle = BLE_HS_CONN_HANDLE_NONE;

            for (size_t i = 0; i < POUCH_CHAR_COUNT; i++)
            {
                if (all_chars[i].subscribed)
                {
                    close(&all_chars[i]);
                    all_chars[i].subscribed = false;
                }
            }
            break;

        case BLE_GAP_EVENT_SUBSCRIBE:
        {
            struct pouch_characteristic *c = char_find_by_val_handle(event->subscribe.attr_handle);
            if (NULL == c)
            {
                break;
            }

            if (event->subscribe.cur_notify)
            {
                ESP_LOGD(TAG, "%p: subscribe (notify) handle=%u", c, event->subscribe.attr_handle);
                _conn_handle = event->subscribe.conn_handle;
                c->subscribed = true;
                c->bearer.maxlen = _pouch_mtu - BT_ATT_OVERHEAD;

                int err = open(c);
                if (err)
                {
                    ESP_LOGE(TAG, "Failed to open characteristic: %d", err);
                }
            }
            else
            {
                ESP_LOGD(TAG, "%p: unsubscribe handle=%u", c, event->subscribe.attr_handle);
                close(c);
                c->subscribed = false;
            }
            break;
        }

        case BLE_GAP_EVENT_MTU:
            _pouch_mtu = event->mtu.value;
            ESP_LOGI(TAG, "MTU updated: %u", _pouch_mtu);
            break;

        default:
            break;
    }

    return 0;
}

/***************************************************
 * Initialization
 **************************************************/

int pouch_gatt_init(void)
{
    int rc;

    ble_svc_gatt_init();

    rc = ble_gatts_count_cfg(pouch_gatt_svcs);
    if (0 != rc)
    {
        ESP_LOGE(TAG, "ble_gatts_count_cfg failed: %d", rc);
        return rc;
    }

    rc = ble_gatts_add_svcs(pouch_gatt_svcs);
    if (0 != rc)
    {
        ESP_LOGE(TAG, "ble_gatts_add_svcs failed: %d", rc);
        return rc;
    }

    ESP_LOGI(TAG, "Pouch GATT service registered");
    return 0;
}
