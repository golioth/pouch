/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include "esp_log.h"
#include "mbedtls/pk.h"
#include "mbedtls/psa_util.h"

#include <pouch/pouch.h>

#include "credentials.h"

static const char *TAG = "credentials";

/* Embedded device certificate (DER) */
extern const uint8_t device_crt_der_start[] asm("_binary_device_crt_der_start");
extern const uint8_t device_crt_der_end[] asm("_binary_device_crt_der_end");

/* Embedded device private key (DER) */
extern const uint8_t device_key_der_start[] asm("_binary_device_key_der_start");
extern const uint8_t device_key_der_end[] asm("_binary_device_key_der_end");

static mbedtls_svc_key_id_t device_key_id = PSA_KEY_ID_NULL;

static int load_private_key(void)
{
    if (device_key_id != PSA_KEY_ID_NULL)
    {
        return 0;
    }

    psa_crypto_init();

    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);

    size_t key_len = device_key_der_end - device_key_der_start;

    int err = mbedtls_pk_parse_key(&pk,
                                   device_key_der_start,
                                   key_len,
                                   NULL,
                                   0,
                                   mbedtls_psa_get_random,
                                   MBEDTLS_PSA_RANDOM_STATE);
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

int credentials_init(struct pouch_config *config)
{
    /* Device certificate */
    config->certificate.buffer = device_crt_der_start;
    config->certificate.size = device_crt_der_end - device_crt_der_start;

    /* Private key */
    int err = load_private_key();
    if (err)
    {
        ESP_LOGE(TAG, "Failed to load private key");
        return err;
    }

    config->private_key = device_key_id;

    ESP_LOGI(TAG, "Credentials loaded (cert=%u bytes)", (unsigned) config->certificate.size);
    return 0;
}
