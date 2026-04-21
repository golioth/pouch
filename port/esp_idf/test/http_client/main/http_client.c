/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <esp_log.h>
#define TAG "http_client"

#include <esp_http_client.h>
#include <esp_tls.h>
#include "credentials.h"
#include "mtls_type.h"

#include <pouch/port.h>
#include <pouch/transport/certificate.h>

#define MAX_HTTP_RECV_BUFFER 512
#define MAX_HTTP_OUTPUT_BUFFER 2048

#define HTTP_TIMEOUT_MS (CONFIG_POUCH_HTTP_TIMEOUT_S * MSEC_PER_SEC)
#define HTTP_PATH_POUCH "/.g/pouch"
#define HTTP_PATH_SERVER_CERT "/.g/server-cert"
#define HTTP_PATH_DEVICE_CERT "/.g/device-cert"

static struct get_server_cert_context
{
    uint8_t cert_buf[CONFIG_POUCH_HTTP_SERVER_CRT_MAX_SIZE];
    size_t pos;
};

#define IN_USE_FLAG BIT(0)
#define SERVER_CERT_DOWNLOADED_BIT BIT(1)

static struct sync_context
{
    pouch_atomic_t flags;
    struct mtls_credentials *mtls_creds;
    struct get_server_cert_context server_cert;
} _sync_ctx;

static void sync_context_init(struct sync_context *sync, struct mtls_credentials *mtls_creds)
{
    pouch_atomic_clear(&sync->flags);
    sync->mtls_creds = mtls_creds;
}

static esp_err_t pouch_server_cert_response_callback(esp_http_client_event_t *evt)
{
    if (NULL == evt->user_data)
    {
        ESP_LOGE(TAG, "Sync context is NULL");
        return ESP_FAIL;
    }

    struct sync_context *sync = (struct sync_context *) evt->user_data;

    if (HTTP_EVENT_ON_FINISH == evt->event_id)
    {
        ESP_LOGI(TAG, "Server cert downloaded: %zu bytes", sync->server_cert.pos);
        return ESP_OK;
    }

    if (HTTP_EVENT_ON_DATA != evt->event_id)
    {
        ESP_LOGI(TAG, "Non-data event: %d", (int) evt->event_id);
        return ESP_OK;
    }

    ESP_LOGD(TAG, "Received http cert response %d", evt->data_len);

    /* FIXME: Handle event where data is not chunked */
    if (!esp_http_client_is_chunked_response(evt->client))
    {

        if (sync->server_cert.pos + evt->data_len > sizeof(sync->server_cert.cert_buf))
        {
            ESP_LOGE(TAG, "Server cert too large for buffer");
            return ESP_ERR_NO_MEM;
        }

        memcpy(sync->server_cert.cert_buf + sync->server_cert.pos, evt->data, evt->data_len);
        sync->server_cert.pos += evt->data_len;
        ESP_LOGI(TAG, "This is a chunked response");
        return ESP_OK;
    }

    return ESP_OK;
}

static int fetch_server_cert(struct sync_context *sync)
{
    if (true == pouch_atomic_test_bit(&sync->flags, SERVER_CERT_DOWNLOADED_BIT))
    {
        return 0;
    }

    sync->server_cert.pos = 0;

    esp_http_client_config_t config = {
        .path = HTTP_PATH_SERVER_CERT,
        .host = CONFIG_POUCH_HTTP_GW_URI,
        .port = CONFIG_POUCH_HTTP_GW_PORT,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .event_handler = pouch_server_cert_response_callback,
        .cert_pem = sync->mtls_creds->cert_pem,
        .cert_len = sync->mtls_creds->cert_len,
        .client_cert_pem = sync->mtls_creds->client_cert_pem,
        .client_cert_len = sync->mtls_creds->client_cert_len,
        .client_key_pem = sync->mtls_creds->client_key_pem,
        .client_key_len = sync->mtls_creds->client_key_len,
        .user_data = sync,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error performing http request %s", esp_err_to_name(err));
        err = -(esp_http_client_get_errno(client));
        esp_http_client_cleanup(client);
        return err;
    }

    esp_http_client_cleanup(client);

    struct pouch_cert cert = {
        .buffer = sync->server_cert.cert_buf,
        .size = sync->server_cert.pos,
    };

    err = pouch_server_certificate_set(&cert);
    if (0 != err)
    {
        ESP_LOGE(TAG, "Failed to set pouch server cert: %i", err);
        return err;
    }

    pouch_atomic_set_bit(&sync->flags, SERVER_CERT_DOWNLOADED_BIT);
    return 0;
}

void http_client_transport_init(struct mtls_credentials *mtls_creds)
{
    sync_context_init(&_sync_ctx, mtls_creds);
}

int http_client_transport_sync(void)
{
    struct sync_context *sync = &_sync_ctx;

    if (NULL == sync->mtls_creds)
    {
        ESP_LOGE(TAG, "mTLS credentials not found; run HTTP transport init.");
        return -EINVAL;
    }

    if (true == pouch_atomic_test_and_set_bit(&sync->flags, IN_USE_FLAG))
    {
        ESP_LOGW(TAG, "Sync already in progress, aborting.");
        return -EACCES;
    }

    int err = fetch_server_cert(sync);
    if (0 != err)
    {
        ESP_LOGE(TAG, "Failed to download server certificate: %d", err);
        goto clear_and_return;
    }

clear_and_return:
    pouch_atomic_clear_bit(&sync->flags, IN_USE_FLAG);
    return err;
}
