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
#include <pouch/transport/downlink.h>
#include <pouch/transport/uplink.h>

#define MAX_HTTP_RECV_BUFFER 512
#define MAX_HTTP_OUTPUT_BUFFER 2048

#define HTTP_TIMEOUT_MS (CONFIG_POUCH_HTTP_TIMEOUT_S * 1000)
#define HTTP_PATH_POUCH "/.g/pouch"
#define HTTP_PATH_SERVER_CERT "/.g/server-cert"
#define HTTP_PATH_DEVICE_CERT "/.g/device-cert"

#define POUCH_FILL_COUNT_LIMIT 100

struct get_server_cert_context
{
    uint8_t cert_buf[CONFIG_POUCH_HTTP_SERVER_CRT_MAX_SIZE];
    size_t pos;
};

#define IN_USE_FLAG BIT(0)
#define SERVER_CERT_DOWNLOADED_BIT BIT(1)
#define POUCH_CERT_UPLOADED_BIT BIT(2)
#define DOWNLINK_IN_PROGRESS BIT(3)

static struct sync_context
{
    pouch_atomic_t flags;
    struct mtls_credentials *mtls_creds;
    struct get_server_cert_context server_cert;

    esp_http_client_handle_t client;
    http_event_handle_cb event_cb;
    char url_buf[CONFIG_POUCH_HTTP_CLIENT_URL_BUF_SIZE];

    struct pouch_uplink *uplink;
    uint8_t scratch[CONFIG_POUCH_HTTP_SCRATCH_BUF_SIZE];

    int64_t downlink_pos;
} _sync_ctx;

static void sync_context_init(struct sync_context *sync, struct mtls_credentials *mtls_creds)
{
    pouch_atomic_clear(&sync->flags);
    sync->mtls_creds = mtls_creds;
    sync->client = NULL;
}

static int set_url(struct sync_context *sync, const char *path)
{
    int expected_len = snprintf(sync->url_buf,
                                sizeof(sync->url_buf),
                                "https://%s:%d%s",
                                CONFIG_POUCH_HTTP_GW_URI,
                                CONFIG_POUCH_HTTP_GW_PORT,
                                path);

    if (expected_len >= sizeof(sync->url_buf))
    {
        /* Adjust CONFIG_POUCH_HTTP_CLIENT_URL_BUF_SIZE */
        ESP_LOGE(TAG, "URL too long for buffer");
        return -ENOMEM;
    }

    return 0;
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
    sync->event_cb = pouch_server_cert_response_callback;

    int err = set_url(sync, HTTP_PATH_SERVER_CERT);
    if (0 != err)
    {
        return err;
    }

    esp_http_client_set_url(sync->client, (char *) sync->url_buf);
    esp_http_client_set_method(sync->client, HTTP_METHOD_GET);

    err = esp_http_client_perform(sync->client);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error performing http request %s", esp_err_to_name(err));
        err = -(esp_http_client_get_errno(sync->client));
        return err;
    }

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

esp_err_t pouch_device_cert_response_callback(esp_http_client_event_t *evt)
{
    if (HTTP_EVENT_ON_FINISH == evt->event_id)
    {
        ESP_LOGI(TAG, "Device cert uploaded: %d", esp_http_client_get_status_code(evt->client));
    }

    return 0;
}

static int upload_pouch_cert(struct sync_context *sync)
{
    if (true == pouch_atomic_test_bit(&sync->flags, POUCH_CERT_UPLOADED_BIT))
    {
        return 0;
    }

    struct pouch_cert device_cert;
    int ret = get_device_cert(&device_cert);
    if (0 != ret)
    {
        ESP_LOGE(TAG, "Unable to read pouch device certificate: %d", ret);
        return ret;
    }

    sync->server_cert.pos = 0;
    sync->event_cb = pouch_device_cert_response_callback;

    int err = set_url(sync, HTTP_PATH_DEVICE_CERT);
    if (0 != err)
    {
        return err;
    }

    esp_http_client_set_url(sync->client, (char *) sync->url_buf);
    esp_http_client_set_method(sync->client, HTTP_METHOD_POST);
    esp_http_client_set_header(sync->client, "Content-Type", "application/octet-stream");
    esp_http_client_set_post_field(sync->client, (char *) device_cert.buffer, device_cert.size);

    err = esp_http_client_perform(sync->client);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error performing http request %s", esp_err_to_name(err));
        err = -(esp_http_client_get_errno(sync->client));
        return err;
    }

    pouch_atomic_set_bit(&sync->flags, POUCH_CERT_UPLOADED_BIT);
    return 0;
}

esp_err_t pouch_uplink_response_callback(esp_http_client_event_t *evt)
{
    struct sync_context *sync = (struct sync_context *) evt->user_data;

    if (HTTP_EVENT_ON_DATA == evt->event_id)
    {
        if (false == pouch_atomic_test_and_set_bit(&sync->flags, DOWNLINK_IN_PROGRESS))
        {
            pouch_uplink_finish(sync->uplink);
            ESP_LOGD(TAG, "Uplink complete");

            sync->uplink = NULL;
            pouch_downlink_start();
        }

        int err = pouch_downlink_push(evt->data, evt->data_len);
        if (0 != err)
        {
            ESP_LOGE(TAG, "Failed to push downlink data: %i", err);
            return ESP_FAIL;
        }

        sync->downlink_pos += evt->data_len;

        if (!esp_http_client_is_chunked_response(evt->client))
        {
            /* Data was not chunked, this is the end of the downlink */
            ESP_LOGD(TAG, "Downlink complete: %" PRIi64 " bytes", sync->downlink_pos);
            pouch_downlink_finish();
            pouch_atomic_clear_bit(&sync->flags, DOWNLINK_IN_PROGRESS);
            return 0;
        }

        /* Data is chunked, feed the watchdog timer between each payload */
        taskYIELD();
    }

    if (HTTP_EVENT_ON_FINISH == evt->event_id)
    {
        if (true == pouch_atomic_test_and_clear_bit(&sync->flags, DOWNLINK_IN_PROGRESS))
        {
            /* This is the end of a chunked downlink */
            ESP_LOGI(TAG, "Received final downlink block");
            pouch_downlink_finish();
            pouch_atomic_clear_bit(&sync->flags, DOWNLINK_IN_PROGRESS);
        }
    }

    return 0;
}

static int pouch_http_client_uplink_payload_send(struct sync_context *sync,
                                                 esp_http_client_handle_t client)
{
    if (NULL == sync)
    {
        ESP_LOGE(TAG, "Sync context cannot be NULL");
        return -EINVAL;
    }

    int err = 0;
    int ret;
    size_t pouch_data_len = sizeof(sync->scratch);
    size_t payload_size = 0;
    enum pouch_result res = POUCH_ERROR;
    char chunk_header[16];

    do
    {
        pouch_data_len = sizeof(sync->scratch);
        res = pouch_uplink_fill(sync->uplink, sync->scratch, &pouch_data_len);
        if (POUCH_ERROR == res)
        {
            ESP_LOGE(TAG, "Error getting pouch data: %d", err);
            return -EINVAL;
        }

        /* Write HEX_CHUNK_SIZE + CRLF to start chunk */
        int chunk_header_len =
            snprintf(chunk_header, sizeof(chunk_header), "%X\r\n", (int) pouch_data_len);
        if (0 > chunk_header_len)
        {
            ESP_LOGE(TAG, "Failed to format chunk size");
            return ESP_ERR_INVALID_SIZE;
        }
        ret = esp_http_client_write(client, chunk_header, chunk_header_len);
        if (0 > ret)
        {
            ESP_LOGE(TAG, "Failed to send chunk header: %d", ret);

            return -(esp_http_client_get_errno(client));
        }

        ret = esp_http_client_write(client, (char *) sync->scratch, pouch_data_len);
        if (0 > ret)
        {
            ESP_LOGE(TAG, "Failed to send data: %d", ret);
            return -(esp_http_client_get_errno(client));
        }
        payload_size += ret;

        /* Write CRLF to complete chunk */
        ret = esp_http_client_write(client, "\r\n", 2);
        if (0 > ret)
        {
            ESP_LOGE(TAG, "Failed to send chunk footer: %d", ret);
            return -(esp_http_client_get_errno(client));
        }

    } while (POUCH_MORE_DATA == res);

    ESP_LOGD(TAG, "Uplink chunks sent: %zu bytes", payload_size);

    /* Write 0 block to finish chunk post */
    ret = esp_http_client_write(client, "0\r\n\r\n", 5);
    if (0 > ret)
    {
        ESP_LOGE(TAG, "Failed to finish chunk post: %d", ret);
        return -(esp_http_client_get_errno(client));
    }

    return 0;
}

static int send_pouch_uplink(struct sync_context *sync)
{
    sync->uplink = pouch_uplink_start();
    if (NULL == sync->uplink)
    {
        ESP_LOGE(TAG, "Failed to start uplink");
        return -ENOMEM;
    }

    sync->downlink_pos = 0;
    sync->event_cb = pouch_uplink_response_callback;

    int err = set_url(sync, HTTP_PATH_POUCH);
    if (0 != err)
    {
        goto finish_and_return;
    }

    esp_http_client_set_url(sync->client, (char *) sync->url_buf);
    esp_http_client_set_method(sync->client, HTTP_METHOD_POST);
    esp_http_client_set_header(sync->client, "Content-Type", "application/octet-stream");

    err = esp_http_client_open(sync->client, -1);
    if (0 > err)
    {
        ESP_LOGE(TAG, "Failed to open uplink: %d", err);
        goto finish_and_return;
    }

    err = pouch_http_client_uplink_payload_send(sync, sync->client);
    if (0 > err)
    {
        ESP_LOGE(TAG, "Failed to send payload: %d", err);
        goto finish_and_return;
    }

    err = esp_http_client_perform(sync->client);
    if (0 > err)
    {
        ESP_LOGE(TAG, "Failed to perform downlink: %d", err);
        goto finish_and_return;
    }

    int status_code = esp_http_client_get_status_code(sync->client);

    if (!IN_RANGE(status_code, 200, 299))
    {
        ESP_LOGE(TAG, "Sync failed: %d", status_code);
        err = -EIO;
        goto finish_and_return;
    }
    else
    {
        ESP_LOGI(TAG, "Sync successful: %d", status_code);
    }

    esp_http_client_close(sync->client);

    err = 0;

finish_and_return:
    if (NULL != sync->uplink)
    {
        pouch_uplink_finish(sync->uplink);
    }

    if (true == pouch_atomic_test_and_clear_bit(&sync->flags, DOWNLINK_IN_PROGRESS))
    {
        /* TODO: pouch_downlink_finish(); */
    }

    return err;
}

void http_client_transport_init(struct mtls_credentials *mtls_creds)
{
    sync_context_init(&_sync_ctx, mtls_creds);
}

esp_err_t event_handler_proxy(esp_http_client_event_t *evt)
{
    if (NULL == evt->user_data)
    {
        ESP_LOGE(TAG, "Sync context cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }

    struct sync_context *sync = (struct sync_context *) evt->user_data;
    return sync->event_cb(evt);
}

esp_http_client_handle_t client_initialize(struct sync_context *sync)
{
    esp_http_client_config_t config = {
        .path = HTTP_PATH_POUCH,
        .host = CONFIG_POUCH_HTTP_GW_URI,
        .port = CONFIG_POUCH_HTTP_GW_PORT,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .event_handler = event_handler_proxy,
        .cert_pem = sync->mtls_creds->cert_pem,
        .cert_len = sync->mtls_creds->cert_len,
        .client_cert_pem = sync->mtls_creds->client_cert_pem,
        .client_cert_len = sync->mtls_creds->client_cert_len,
        .client_key_pem = sync->mtls_creds->client_key_pem,
        .client_key_len = sync->mtls_creds->client_key_len,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .user_data = sync,
    };

    ESP_LOGI(TAG, "Initializing http client");
    return esp_http_client_init(&config);
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

    if (NULL == sync->client)
    {
        sync->client = client_initialize(sync);
        if (NULL == sync->client)
        {
            ESP_LOGE(TAG, "Failed to initialize http client");
            pouch_atomic_clear_bit(&sync->flags, IN_USE_FLAG);
            return -ENOENT;
        }
    }

    int err = fetch_server_cert(sync);
    if (0 != err)
    {
        ESP_LOGE(TAG, "Failed to download server certificate: %d", err);
        goto clear_and_return;
    }

    err = upload_pouch_cert(sync);
    if (0 != err)
    {
        ESP_LOGE(TAG, "Failed to upload Pouch certificate: %d", err);
        goto clear_and_return;
    }

    err = send_pouch_uplink(sync);
    if (0 != err)
    {
        ESP_LOGE(TAG, "Failed to send Pouch uplink: %d", err);
        goto clear_and_return;
    }

clear_and_return:
    pouch_atomic_clear_bit(&sync->flags, IN_USE_FLAG);

    if (0 != err)
    {
        ESP_LOGI(TAG, "Freeing http client");
        esp_http_client_cleanup(sync->client);
        sync->client = NULL;
    }
    return err;
}
