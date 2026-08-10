/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Embedded device credentials for the rpmsg example.
 *
 * The key and certificate below are auto-generated placeholders (a self-signed
 * P-256 key pair) so the example is self-contained and builds without a
 * provisioned filesystem. A real deployment must provision a device
 * certificate signed by a CA the cloud trusts (see the coap_client / ble_gatt
 * examples for the filesystem-based provisioning pattern).
 */

#include "credentials.h"

#include <mbedtls/pk.h>
#include <mbedtls/psa_util.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(credentials, LOG_LEVEL_INF);

static const uint8_t device_key[] = {
#include "device_key.der.inc"
};

static const uint8_t device_crt[] = {
#include "device_crt.der.inc"
};

static psa_key_id_t import_raw_pk(const uint8_t *private_key, size_t size)
{
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);

    int err = mbedtls_pk_parse_key(&pk,
                                   private_key,
                                   size,
                                   NULL,
                                   0,
                                   mbedtls_psa_get_random,
                                   MBEDTLS_PSA_RANDOM_STATE);
    if (err)
    {
        LOG_ERR("Failed to parse key: -0x%x", -err);
        mbedtls_pk_free(&pk);
        return PSA_KEY_ID_NULL;
    }

    psa_key_attributes_t attrs = PSA_KEY_ATTRIBUTES_INIT;

    psa_set_key_algorithm(&attrs, PSA_ALG_ECDH);
    psa_set_key_type(&attrs, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_usage_flags(&attrs, PSA_KEY_USAGE_DERIVE);

    psa_key_id_t key_id;
    err = mbedtls_pk_import_into_psa(&pk, &attrs, &key_id);
    mbedtls_pk_free(&pk);
    if (err)
    {
        LOG_ERR("Failed to import private key: -0x%x", -err);
        return PSA_KEY_ID_NULL;
    }

    return key_id;
}

psa_key_id_t load_private_key(void)
{
    return import_raw_pk(device_key, sizeof(device_key));
}

int load_certificate(struct pouch_cert *cert)
{
    cert->buffer = device_crt;
    cert->size = sizeof(device_crt);
    return 0;
}
