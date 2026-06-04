/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "mbedtls/base64.h"
#include "mbedtls/pk.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <pouch/pouch.h>

#include "credentials.h"

static const char *TAG = "credentials";

#define STORAGE_NAMESPACE "client_creds"
#define MAX_CRED_LEN 500

#define CRED_KEY_CRT_DER "crt_der"
#define CRED_KEY_KEY_DER "key_der"

static uint8_t *crt_der_buf;
static size_t crt_der_len;

static uint8_t *key_der_buf;
static size_t key_der_len;

static mbedtls_svc_key_id_t device_key_id = PSA_KEY_ID_NULL;

/***************************************************
 * NVS helpers
 **************************************************/

static int nvs_load_blob(nvs_handle_t handle, const char *key, uint8_t **buf, size_t *len)
{
    *buf = NULL;
    *len = 0;

    size_t required = 0;
    int err = nvs_get_blob(handle, key, NULL, &required);
    if (0 != err)
    {
        return err;
    }

    *buf = malloc(required);
    if (NULL == *buf)
    {
        return -ENOMEM;
    }

    err = nvs_get_blob(handle, key, *buf, &required);
    if (0 != err)
    {
        free(*buf);
        *buf = NULL;
        return err;
    }

    *len = required;
    return 0;
}

static int nvs_store_blob(const char *key, const uint8_t *data, size_t len)
{
    nvs_handle_t handle;
    int err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &handle);
    if (0 != err)
    {
        ESP_LOGE(TAG, "Failed to open NVS: 0x%04x", (unsigned) -err);
        return err;
    }

    err = nvs_set_blob(handle, key, data, len);
    if (0 != err)
    {
        ESP_LOGE(TAG, "Failed to store %s: 0x%04x", key, (unsigned) -err);
    }
    else
    {
        nvs_commit(handle);
    }

    nvs_close(handle);
    return err;
}

/***************************************************
 * Console set functions (base64 -> DER -> NVS)
 **************************************************/

static int cred_set_pki(const char *key, const char *b64_der)
{
    size_t b64_len = strlen(b64_der);
    size_t max_decoded = b64_len; /* generous upper bound */

    uint8_t *decoded = malloc(max_decoded);
    if (NULL == decoded)
    {
        ESP_LOGE(TAG, "Failed to allocate decode buffer");
        return -ENOMEM;
    }

    size_t decoded_len = 0;
    int ret = mbedtls_base64_decode(decoded,
                                    max_decoded,
                                    &decoded_len,
                                    (const unsigned char *) b64_der,
                                    b64_len);
    if (0 != ret)
    {
        ESP_LOGE(TAG, "Failed to decode base64: %d", ret);
        free(decoded);
        return ret;
    }

    ret = nvs_store_blob(key, decoded, decoded_len);
    if (0 == ret)
    {
        ESP_LOGI(TAG, "Credential stored (%zu bytes)", decoded_len);
    }

    free(decoded);
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

/***************************************************
 * Load + status display
 **************************************************/

static bool print_check(const void *buf, const char *msg)
{
    if (NULL == buf)
    {
        printf("❌ %s Failed to Load\n", msg);
        return false;
    }

    printf("✅ %s Loaded\n", msg);
    return true;
}

static int load_credentials_from_nvs(void)
{
    nvs_handle_t handle;
    int err = nvs_open(STORAGE_NAMESPACE, NVS_READONLY, &handle);
    if (0 != err)
    {
        /* Namespace doesn't exist yet — no credentials stored */
        crt_der_buf = NULL;
        key_der_buf = NULL;
        goto print_status;
    }

    nvs_load_blob(handle, CRED_KEY_CRT_DER, &crt_der_buf, &crt_der_len);
    nvs_load_blob(handle, CRED_KEY_KEY_DER, &key_der_buf, &key_der_len);
    nvs_close(handle);

print_status:
    printf("\n");
    bool all_loaded = true;
    all_loaded &= print_check(crt_der_buf, "Device CRT");
    all_loaded &= print_check(key_der_buf, "Device KEY");
    printf("\n");

    if (!all_loaded)
    {
        printf(
            "\t*****************************************************\n"
            "\t* Credentials missing. Use console commands:        *\n"
            "\t*   crt <base64_der>                                *\n"
            "\t*   key <base64_der>                                *\n"
            "\t* Then type 'reset' to reboot.                      *\n"
            "\t*****************************************************\n"
            "\n");
        return -ENOENT;
    }

    return 0;
}

/***************************************************
 * Private key import to PSA
 **************************************************/

static int load_private_key(void)
{
    if (PSA_KEY_ID_NULL != device_key_id)
    {
        return 0;
    }

    psa_crypto_init();

    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);

    int err = mbedtls_pk_parse_key(&pk, key_der_buf, key_der_len, NULL, 0);
    if (err)
    {
        ESP_LOGE(TAG, "Failed to parse private key: -0x%04x", (unsigned) -err);
        mbedtls_pk_free(&pk);
        return -EINVAL;
    }

    psa_key_attributes_t attrs = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_algorithm(&attrs, PSA_ALG_ECDH);
    psa_set_key_type(&attrs, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_usage_flags(&attrs, PSA_KEY_USAGE_DERIVE);

    err = mbedtls_pk_import_into_psa(&pk, &attrs, &device_key_id);
    mbedtls_pk_free(&pk);

    if (err)
    {
        ESP_LOGE(TAG, "Failed to import private key into PSA: -0x%04x", (unsigned) -err);
        return -EINVAL;
    }

    return 0;
}

/***************************************************
 * Public API
 **************************************************/

int credentials_init(struct pouch_config *config)
{
    int err = load_credentials_from_nvs();
    if (0 != err)
    {
        return err;
    }

    /* Device certificate */
    config->certificate.buffer = crt_der_buf;
    config->certificate.size = crt_der_len;

    /* Private key */
    err = load_private_key();
    if (err)
    {
        ESP_LOGE(TAG, "Failed to load private key");
        return err;
    }

    config->private_key = device_key_id;

    ESP_LOGI(TAG, "Credentials loaded (cert=%u bytes)", (unsigned) config->certificate.size);
    return 0;
}
