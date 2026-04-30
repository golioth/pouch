/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <esp_log.h>
#include "credentials_nvs.h"
#include <mbedtls/base64.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <string.h>

#define TAG "credentials_nvs"

#define STORAGE_NAMESPACE "client_creds"
#define MAX_CRED_LEN 500

#define CRED_KEY_WIFI_SSID "wifi_ssid"
#define CRED_KEY_WIFI_PSK "wifi_psk"
#define CRED_KEY_CRT_DER "crt_der"
#define CRED_KEY_KEY_DER "key_der"

static struct pouch_cert _ssid;
static struct pouch_cert _psk;
static struct pouch_cert _crt_der;
static struct pouch_cert _key_der;

static void print_credential_help(void)
{
    printf(
        "\n"
        "\t*****************************************************\n"
        "\t* Failed to load credentials. Type \"help\" for       *\n"
        "\t* guidance or follow the README.md instructions for *\n"
        "\t* setting by flashing an NVS binary.                *\n"
        "\t*****************************************************\n"
        "\n");
}

static void cred_init(struct pouch_cert *cred)
{
    cred->buffer = NULL;
    cred->size = 0;
}

static void cred_free(struct pouch_cert *cred)
{
    free((void *) cred->buffer);
    cred_init(cred);
}

static int cred_string_load(nvs_handle_t handle, const char *key, struct pouch_cert *cred)
{
    cred->size = MAX_CRED_LEN;
    int err = nvs_get_str(handle, key, NULL, &cred->size);
    if (0 != err)
    {
        ESP_LOGE(TAG, "Failed to read %s len: 0x%04X", key, err);
        cred_init(cred);
        return err;
    }

    cred->buffer = (uint8_t *) malloc(cred->size);
    if (NULL == cred->buffer)
    {
        cred_init(cred);
        return ESP_ERR_NO_MEM;
    }

    err = nvs_get_str(handle, key, (char *) cred->buffer, &cred->size);
    if (0 != err)
    {
        ESP_LOGE(TAG, "Failed to load %s from NVS: 0x%04X", key, err);
        cred_free(cred);
        return err;
    }

    return 0;
}

static int cred_string_store(nvs_handle_t handle, const char *key, char *buf)
{
    int err = nvs_set_str(handle, key, (char *) buf);
    if (0 != err)
    {
        ESP_LOGE(TAG, "Failed to set %s len: %i", key, err);
        return err;
    }
    return 0;
}

static int cred_binary_store(nvs_handle_t handle, const char *key, struct pouch_cert *cred)
{

    int err = nvs_set_blob(handle, key, cred->buffer, cred->size);
    if (0 != err)
    {
        ESP_LOGE(TAG, "Failed to set %s len: %i", key, err);
        return err;
    }
    return 0;
}

static int cred_binary_load(nvs_handle_t handle, const char *key, struct pouch_cert *cred)
{
    cred_init(cred);
    cred->size = MAX_CRED_LEN;
    int err = nvs_get_blob(handle, key, NULL, &cred->size);
    if (0 != err)
    {
        ESP_LOGE(TAG, "Failed to read %s len: 0x%04X", key, err);
        cred_init(cred);
        return err;
    }

    cred->buffer = (uint8_t *) malloc(cred->size);
    if (NULL == cred->buffer)
    {
        cred_init(cred);
        return ESP_ERR_NO_MEM;
    }

    err = nvs_get_blob(handle, key, (void *) cred->buffer, &cred->size);
    if (0 != err)
    {
        ESP_LOGE(TAG, "Failed to load %s from NVS: 0x%04X", key, err);
        cred_free(cred);
        return err;
    }

    return 0;
}

static bool print_check(struct pouch_cert *cred, const char *msg)
{
    if (0 == cred->size)
    {
        printf("❌ %s Failed to Load\n", msg);
        return false;
    }

    printf("✅ %s Loaded\n", msg);
    return true;
}

static bool cred_load_check_print(void)
{
    bool all_loaded = true;
    printf("\n");
    all_loaded &= print_check(&_ssid, "WiFi SSID");
    all_loaded &= print_check(&_psk, "WiFi PSK");
    all_loaded &= print_check(&_crt_der, "Device CRT");
    all_loaded &= print_check(&_key_der, "Device KEY");
    printf("\n");
    return all_loaded;
}

int cred_nvs_load_all(void)
{
    cred_init(&_ssid);
    cred_init(&_psk);
    cred_init(&_crt_der);
    cred_init(&_key_der);

    nvs_handle_t handle;
    int err = nvs_open(STORAGE_NAMESPACE, NVS_READONLY, &handle);
    if (0 != err)
    {
        ESP_LOGE(TAG, "Failed to initialize NVS handle");
        print_credential_help();
        return err;
    }

    cred_string_load(handle, CRED_KEY_WIFI_SSID, &_ssid);
    cred_string_load(handle, CRED_KEY_WIFI_PSK, &_psk);
    cred_binary_load(handle, CRED_KEY_CRT_DER, &_crt_der);
    cred_binary_load(handle, CRED_KEY_KEY_DER, &_key_der);

    nvs_close(handle);

    if (true != cred_load_check_print())
    {
        print_credential_help();
        nvs_close(handle);
        return -ENOENT;
    }

    return 0;
}

static int cred_set_wifi(char *key, char *buf)
{
    nvs_handle_t handle;
    int err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &handle);
    if (0 != err)
    {
        ESP_LOGE(TAG, "Failed to open NVS: 0x%04x", -err);
        return err;
    }
    return cred_string_store(handle, key, buf);

    nvs_close(handle);
}

int cred_set_wifi_ssid(const char *ssid)
{
    return cred_set_wifi(CRED_KEY_WIFI_SSID, (char *) ssid);
}

int cred_set_wifi_psk(const char *psk)
{
    return cred_set_wifi(CRED_KEY_WIFI_PSK, (char *) psk);
}

static int cred_set_pki(char *key, const char *b64_der)
{
    nvs_handle_t handle;
    int ret = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &handle);
    if (0 != ret)
    {
        ESP_LOGE(TAG, "Failed to open NVS: 0x%04x", -ret);
        return ret;
    }

    struct pouch_cert cred;
    cred_init(&cred);
    size_t b64_der_len = strlen(b64_der) + 1;
    cred.buffer = (uint8_t *) malloc(b64_der_len);
    if (NULL == cred.buffer)
    {
        ESP_LOGE(TAG, "Failed to allocate credential buffer");
        ret = ESP_ERR_NO_MEM;
        goto free_and_return;
    }

    ret = mbedtls_base64_decode((unsigned char *) cred.buffer,
                                b64_der_len,
                                &cred.size,
                                (unsigned char *) b64_der,
                                strlen(b64_der));
    if (0 != ret)
    {
        ESP_LOGE(TAG, "Failed to decode base64: %d", ret);
        goto free_and_return;
    }

    ret = cred_binary_store(handle, key, &cred);
    if (0 != ret)
    {
        ESP_LOGE(TAG, "Failed to store credential: 0x%04x", -ret);
        goto free_and_return;
    }

    ESP_LOGI(TAG, "Credential stored (%zu bytes)", cred.size);

free_and_return:
    cred_free(&cred);
    nvs_close(handle);
    return ret;
}

int cred_set_device_crt(const char *b64_der)
{
    return cred_set_pki(CRED_KEY_CRT_DER, b64_der);
}

int cred_set_device_key(const char *b64_der)
{
    return cred_set_pki(CRED_KEY_KEY_DER, b64_der);
}


const char *cred_get_wifi_ssid(void)
{
    if (_ssid.size == 0)
    {
        return NULL;
    }

    return (char *) _ssid.buffer;
}

const char *cred_get_wifi_psk(void)
{
    if (_psk.size == 0)
    {
        return NULL;
    }
    return (char *) _psk.buffer;
}

const uint8_t *cred_get_device_crt_der(size_t *len)
{
    if (_crt_der.size == 0)
    {
        return NULL;
    }
    *len = _crt_der.size;
    return _crt_der.buffer;
}

const uint8_t *cred_get_device_key_der(size_t *len)
{
    if (_key_der.size == 0)
    {
        return NULL;
    }
    *len = _key_der.size;
    return _key_der.buffer;
}
