/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <esp_log.h>
#include <errno.h>
#include <pouch/pouch.h>
#include <pouch/types.h>
#include "mbedtls/pk.h"
#include "mbedtls/psa_util.h"

#define TAG "credentials"
extern const char device_crt_der_start[] asm("_binary_placeholder_device_crt_der_start");
extern const char device_crt_der_end[] asm("_binary_placeholder_device_crt_der_end");
extern const char device_key_der_start[] asm("_binary_placeholder_device_key_der_start");
extern const char device_key_der_end[] asm("_binary_placeholder_device_key_der_end");

/* Server CA Cert in PEM format for mTLS */
extern const char server_ca_cert_pem_start[] asm("_binary_server_ca_cert_pem_start");
extern const char server_ca_cert_pem_end[] asm("_binary_server_ca_cert_pem_end");

static mbedtls_svc_key_id_t _device_key_id = PSA_KEY_ID_NULL;

int get_device_cert(struct pouch_cert *cert)
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
