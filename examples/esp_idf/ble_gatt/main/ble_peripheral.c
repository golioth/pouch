/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ble_peripheral.h"

#include <stdbool.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nimble/nimble_npl.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"

/*
 * NimBLE's store/config module provides ble_store_config_init() but does not
 * declare it in a public header (ESP-IDF issue #15603). Declare it here so the
 * SM has a working RAM-based key store during pairing.
 */
void ble_store_config_init(void);

#include <pouch/transport/bluetooth/gatt.h>
#include <pouch/transport/ble_gatt/peripheral.h>

static const char *TAG = "ble_peripheral";

static uint8_t own_addr_type;

static TimerHandle_t sync_request_timer;
static StaticTimer_t sync_request_timer_buf;
static struct ble_npl_event sync_request_event;

static void sync_request_event_fn(struct ble_npl_event *ev)
{
    ble_peripheral_request_gateway(true);
}

static void sync_request_timer_cb(TimerHandle_t timer)
{
    /* Enqueue onto NimBLE host task's event queue to avoid timer stack overflow */
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &sync_request_event);
}

static struct pouch_gatt_adv service_data = POUCH_GATT_ADV_DATA_INIT;

/* Forward declaration */
static int gap_event_handler(struct ble_gap_event *event, void *arg);

/***************************************************
 * Advertising
 **************************************************/

static void start_advertising(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    int rc;

    memset(&fields, 0, sizeof(fields));

    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *) CONFIG_EXAMPLE_BT_DEVICE_NAME;
    fields.name_len = strlen(CONFIG_EXAMPLE_BT_DEVICE_NAME);
    fields.name_is_complete = 1;
    fields.svc_data_uuid16 = (uint8_t *) &service_data;
    fields.svc_data_uuid16_len = sizeof(service_data);

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to set adv fields: %d", rc);
        return;
    }

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = 0x20;
    adv_params.itvl_max = 0x40;

    rc = ble_gap_adv_start(own_addr_type,
                           NULL,
                           BLE_HS_FOREVER,
                           &adv_params,
                           gap_event_handler,
                           NULL);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to start advertising: %d", rc);
        return;
    }

    ESP_LOGI(TAG, "Advertising started");
}

/***************************************************
 * GAP event handler
 **************************************************/

static int gap_event_handler(struct ble_gap_event *event, void *arg)
{
    /* Let the transport layer handle connection/subscribe/mtu events */
    int rc = pouch_gatt_gap_event(event, arg);

    switch (event->type)
    {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status != 0)
            {
                /* Connection failed; resume advertising */
                start_advertising();
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            /* Resume advertising without sync flag; schedule re-request */
            ble_peripheral_request_gateway(false);
            start_advertising();
            xTimerStart(sync_request_timer, 0);
            break;

        case BLE_GAP_EVENT_ADV_COMPLETE:
            start_advertising();
            break;

        case BLE_GAP_EVENT_ENC_CHANGE:
            if (event->enc_change.status == 0)
            {
                ESP_LOGI(TAG, "Encryption established");
            }
            else
            {
                ESP_LOGE(TAG, "Encryption failed: %d", event->enc_change.status);
            }
            break;

        case BLE_GAP_EVENT_REPEAT_PAIRING:
        {
            /* Remove old bond and allow re-pairing */
            struct ble_gap_conn_desc desc;
            ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
            ble_store_util_delete_peer(&desc.peer_id_addr);
            return BLE_GAP_REPEAT_PAIRING_RETRY;
        }

        default:
            break;
    }

    return rc;
}

/***************************************************
 * NimBLE host sync callback
 **************************************************/

static void on_sync(void)
{
    int rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to infer address type: %d", rc);
        return;
    }

    start_advertising();
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "NimBLE host reset: %d", reason);
}

/***************************************************
 * NimBLE host task
 **************************************************/

static void nimble_host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/***************************************************
 * Public API
 **************************************************/

void ble_peripheral_request_gateway(bool request)
{
    pouch_gatt_adv_req_sync(&service_data, request);

    /* Stop and restart advertising with updated data */
    if (ble_gap_adv_active())
    {
        ble_gap_adv_stop();
        start_advertising();
    }
}

int ble_peripheral_init(void)
{
    int rc;

    rc = nimble_port_init();
    if (rc != ESP_OK)
    {
        ESP_LOGE(TAG, "NimBLE port init failed: %d", rc);
        return -1;
    }

    /* NimBLE host configuration */
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    /* Security Manager configuration */
    ble_hs_cfg.sm_bonding = 1; /* enable bonding */
    ble_hs_cfg.sm_mitm = 0;    /* no MITM protection required */
    ble_hs_cfg.sm_sc = 1;      /* require LE Secure Connections */
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    /* Initialize GATT services */
    ble_svc_gap_init();
    rc = pouch_gatt_init();
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Pouch GATT init failed: %d", rc);
        return -1;
    }

    /* Set device name */
    rc = ble_svc_gap_device_name_set(CONFIG_EXAMPLE_BT_DEVICE_NAME);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to set device name: %d", rc);
        return -1;
    }

    /* Initialize RAM-based key store (required for SM pairing) */
    ble_store_config_init();

    /* Create sync request timer + event for deferred NimBLE work */
    ble_npl_event_init(&sync_request_event, sync_request_event_fn, NULL);
    sync_request_timer = xTimerCreateStatic("sync_req",
                                            pdMS_TO_TICKS(CONFIG_EXAMPLE_SYNC_PERIOD_S * 1000),
                                            pdFALSE, /* one-shot */
                                            NULL,
                                            sync_request_timer_cb,
                                            &sync_request_timer_buf);

    /* Start NimBLE host task */
    nimble_port_freertos_init(nimble_host_task);

    return 0;
}

int ble_peripheral_start(void)
{
    pouch_gatt_adv_req_sync(&service_data, false);
    /* Advertising starts automatically via on_sync callback */
    return 0;
}
