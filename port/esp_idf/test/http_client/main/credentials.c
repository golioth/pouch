/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <esp_log.h>
#include <errno.h>
#include <pouch/pouch.h>
#include <pouch/transport/http/client.h>
#include <pouch/types.h>
#include <string.h>
#include "mbedtls/pk.h"
#include "mbedtls/pem.h"
#include "mbedtls/psa_util.h"
#include "mbedtls/x509_crt.h"

#define TAG "credentials"
extern const char device_crt_der_start[] asm("_binary_device_crt_der_start");
extern const char device_crt_der_end[] asm("_binary_device_crt_der_end");
extern const char device_key_der_start[] asm("_binary_device_key_der_start");
extern const char device_key_der_end[] asm("_binary_device_key_der_end");

/* Server CA Cert in PEM format for mTLS */
extern const char server_ca_cert_pem_start[] asm("_binary_server_ca_cert_pem_start");
extern const char server_ca_cert_pem_end[] asm("_binary_server_ca_cert_pem_end");

static mbedtls_svc_key_id_t _device_key_id = PSA_KEY_ID_NULL;

static int get_device_cert(struct pouch_cert *cert)
{
    cert->buffer = (uint8_t *) device_crt_der_start;
    cert->size = device_crt_der_end - device_crt_der_start;

    return 0;
}

static int load_device_pk(mbedtls_svc_key_id_t *key_id)
{
    if (PSA_KEY_ID_NULL != *key_id)
    {
        return 0;
    }

    psa_crypto_init();
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);

    int err = mbedtls_pk_parse_key(&pk,
                                   (unsigned char *) device_key_der_start,
                                   device_key_der_end - device_key_der_start,
                                   NULL,
                                   0,
                                   mbedtls_psa_get_random,
                                   MBEDTLS_PSA_RANDOM_STATE);
    if (err)
    {
        ESP_LOGE(TAG, "Failed to parse key: -0x%" PRIx32, (uint32_t) -err);
        return -EINVAL;
    }

    psa_key_attributes_t attrs = PSA_KEY_ATTRIBUTES_INIT;

    psa_set_key_algorithm(&attrs, PSA_ALG_ECDH);
    psa_set_key_type(&attrs, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_usage_flags(&attrs, PSA_KEY_USAGE_DERIVE);

    err = mbedtls_pk_import_into_psa(&pk, &attrs, key_id);
    if (err)
    {
        ESP_LOGE(TAG, "Failed to import private key: -0x%" PRIx32, (uint32_t) -err);
        return -EINVAL;
    }

    return 0;
}

psa_key_id_t get_device_pk_id(void)
{
    return _device_key_id;
}

int fill_pouch_config(struct pouch_config *config)
{
    int err = get_device_cert(&config->certificate);
    if (0 != err)
    {
        ESP_LOGE(TAG, "Failed to load device cert");
        return err;
    }

    err = load_device_pk(&_device_key_id);
    if (0 != err)
    {
        ESP_LOGE(TAG, "Failed to load device key");
        return err;
    }

    config->private_key = get_device_pk_id();
    if (config->private_key == PSA_KEY_ID_NULL)
    {
        ESP_LOGE(TAG, "Failed to get device key id");
        return -1;
    }

    return 0;
}

/* Buffer and function to convert device key der to pem */
/* This can be removed after ESP-IDF v6 (includes der support for HTTP client) */
static char device_crt_pem[768];

static int convert_device_pk_der_to_pem(char *pem_buf, size_t pem_buf_len)
{
    psa_crypto_init();
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);

    int err = mbedtls_pk_parse_key(&pk,
                                   (unsigned char *) device_key_der_start,
                                   device_key_der_end - device_key_der_start,
                                   NULL,
                                   0,
                                   mbedtls_psa_get_random,
                                   MBEDTLS_PSA_RANDOM_STATE);

    if (0 != err)
    {
        ESP_LOGE(TAG, "Failed to parse device.key.der: %d", err);
        return err;
    }

    err = mbedtls_pk_write_key_pem(&pk, (unsigned char *) pem_buf, pem_buf_len);
    if (0 != err)
    {
        ESP_LOGE(TAG, "Failed to convert device.key.der to PEM: %d", err);
        return err;
    }

    mbedtls_pk_free(&pk);

    return 0;
}

/* Buffer and function to convert device crt der to pem */
/* This can be removed after ESP-IDF v6 (includes der support for HTTP client) */
static char device_key_pem[310];

static int convert_device_cert_der_to_pem(char *pem_buf, size_t pem_buf_len)
{
    psa_crypto_init();
    mbedtls_x509_crt crt;
    mbedtls_x509_crt_init(&crt);

    int err = mbedtls_x509_crt_parse_der(&crt,
                                         (unsigned char *) device_crt_der_start,
                                         device_key_der_end - device_crt_der_start);
    if (0 != err)
    {
        ESP_LOGE(TAG, "Failed to parse device.crt.der: %d", err);
        return err;
    }

    size_t pem_written = 0;
    err = mbedtls_pem_write_buffer("-----BEGIN CERTIFICATE-----\n",
                                   "-----END CERTIFICATE-----\n",
                                   crt.raw.p,
                                   crt.raw.len,
                                   (unsigned char *) pem_buf,
                                   pem_buf_len,
                                   &pem_written);
    if (0 != err)
    {
        ESP_LOGE(TAG, "Failed to convert device.crt.der to PEM: %d", err);
        return err;
    }

    mbedtls_x509_crt_free(&crt);
    return 0;
}

int fill_mtls_credentials(struct mtls_credentials *creds)
{
    creds->cert_pem = server_ca_cert_pem_start;
    creds->cert_pem_len = server_ca_cert_pem_end - server_ca_cert_pem_start;

    if ((NULL == creds->cert_pem) || (0 == creds->cert_pem_len))
    {
        ESP_LOGE(TAG, "Failed to load mTLS server CA cert");
        return -ENOENT;
    }

    int err = load_device_pk(&_device_key_id);
    if (0 != err)
    {
        ESP_LOGE(TAG, "Failed to load device key");
        return err;
    }

    /* NOTE: DER support in ESP-IDF HTTP Client wasn't added until v6 */
    /* Device DER files need to be converted to PEM */
    err = convert_device_pk_der_to_pem(device_key_pem, sizeof(device_key_pem));
    if (0 != err)
    {
        return err;
    }
    creds->client_key_pem = device_key_pem;
    /* Get size including null terminator */
    creds->client_key_pem_len = strlen(device_key_pem) + 1;

    creds->client_key_der = device_key_der_start;
    creds->client_key_der_len = device_key_der_end - device_key_der_start;

    err = convert_device_cert_der_to_pem(device_crt_pem, sizeof(device_crt_pem));
    if (0 != err)
    {
        return err;
    }
    creds->client_cert_pem = device_crt_pem;
    /* Get size including null terminator */
    creds->client_cert_pem_len = strlen(device_crt_pem) + 1;

    creds->client_cert_der = device_crt_der_start;
    creds->client_cert_der_len = device_crt_der_end - device_crt_der_start;

    return 0;
}
