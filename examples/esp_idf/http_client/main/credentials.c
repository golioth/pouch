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
#include "mbedtls/x509_crt.h"
#include "credentials_nvs.h"

#define TAG "credentials"

/* Server CA Cert in PEM format for mTLS */
extern const char server_ca_cert_pem_start[] asm("_binary_server_ca_cert_pem_start");
extern const char server_ca_cert_pem_end[] asm("_binary_server_ca_cert_pem_end");

static mbedtls_svc_key_id_t _device_key_id = PSA_KEY_ID_NULL;

int credentials_init(void)
{
    /* Load all credentials from NVS */
    return cred_nvs_load_all();
}

static int get_device_cert(struct pouch_cert *cert)
{
    cert->buffer = cred_get_device_crt_der(&cert->size);

    return (NULL == cert->buffer) ? -ENOENT : 0;
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

    struct pouch_cert key_der;
    key_der.buffer = cred_get_device_key_der(&key_der.size);
    if (NULL == key_der.buffer)
    {
        return -ENOENT;
    }

    int err = mbedtls_pk_parse_key(&pk, (unsigned char *) key_der.buffer, key_der.size, NULL, 0);
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

int fill_mtls_credentials(struct mtls_credentials *creds)
{
    creds->cert_pem = server_ca_cert_pem_start;
    creds->cert_pem_len = server_ca_cert_pem_end - server_ca_cert_pem_start;
    creds->cert_cn = NULL; /* Use hostname for server cert common name */

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

    creds->client_key_der = (const char *) cred_get_device_key_der(&creds->client_key_der_len);
    if (NULL == creds->client_key_der)
    {
        return -ENOENT;
    }

    creds->client_cert_der = (const char *) cred_get_device_crt_der(&creds->client_cert_der_len);
    if (NULL == creds->client_cert_der)
    {
        ESP_LOGE(TAG, "Failed to load device cert");
        return -ENOENT;
    }

    return 0;
}
