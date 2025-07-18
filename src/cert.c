/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cert.h"
#include <psa/crypto.h>
#include <zephyr/kernel.h>
#include <pouch/transport/certificate.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(cert, LOG_LEVEL_DBG);

static const uint8_t raw_ca_cert[] = {
#if IS_ENABLED(CONFIG_POUCH_VALIDATE_SERVER_CERT)
#include "golioth_ca_cert.inc"
#endif
};

static struct
{
    struct pouch_cert certificate;
    uint8_t ref[CERT_REF_LEN];
} device;
static struct
{
    struct pubkey pubkey;
    struct
    {
        uint8_t data[CERT_SERIAL_MAXLEN];
        size_t len;
    } serial;
} server_cert;

static inline bool cert_is_valid(const struct pouch_cert *cert)
{
    return cert != NULL && cert->der != NULL && cert->size > 0;
}

static int parse_x509_cert(const struct pouch_cert *cert, mbedtls_x509_crt *out)
{
    if (!cert_is_valid(cert))
    {
        return -EINVAL;
    }

    mbedtls_x509_crt_init(out);

    int ret = mbedtls_x509_crt_parse_der_nocopy(out, cert->der, cert->size);
    if (ret != 0)
    {
        LOG_ERR("Failed to parse certificate: %x", -ret);
        return -EIO;
    }

    return 0;
}

static mbedtls_x509_crt *load_ca_cert(void)
{
    static mbedtls_x509_crt ca_cert;
    static bool loaded;

    // lazy load the CA cert:
    if (loaded)
    {
        return &ca_cert;
    }

    const struct pouch_cert ca_cert_data = {
        .der = raw_ca_cert,
        .size = sizeof(raw_ca_cert),
    };

    int err = parse_x509_cert(&ca_cert_data, &ca_cert);
    if (err)
    {
        return NULL;
    }

    loaded = true;

    return &ca_cert;
}

static int generate_ref(const struct pouch_cert *cert, uint8_t cert_ref[CERT_REF_LEN])
{
    size_t hash_length;
    psa_status_t status = psa_hash_compute(PSA_ALG_SHA_256,
                                           cert->der,
                                           cert->size,
                                           cert_ref,
                                           CERT_REF_LEN,
                                           &hash_length);
    if (status != PSA_SUCCESS || hash_length != CERT_REF_LEN)
    {
        return -EIO;
    }

    return 0;
}

static int authenticate_server_cert(mbedtls_x509_crt *cert)
{
    mbedtls_x509_crt *ca_cert = load_ca_cert();
    if (ca_cert == NULL)
    {
        LOG_ERR("Failed loading server CA cert");
        return -EIO;
    }

    uint32_t flags;
    int ret = mbedtls_x509_crt_verify(cert, ca_cert, NULL, NULL, &flags, NULL, NULL);
    if (ret != 0)
    {
        LOG_ERR("Failed verifying server cert: 0x%x", -ret);
        return -EPERM;
    }

    return 0;
}

int cert_device_set(const struct pouch_cert *cert)
{
    if (!cert_is_valid(cert))
    {
        return -EINVAL;
    }

    int err = generate_ref(cert, device.ref);
    if (err)
    {
        return err;
    }

    device.certificate = *cert;

    return 0;
}

int cert_server_set(const struct pouch_cert *certbuf)
{
    int err;

    if (!cert_is_valid(certbuf))
    {
        return -EINVAL;
    }

    mbedtls_x509_crt parsed_cert;
    err = parse_x509_cert(certbuf, &parsed_cert);
    if (err)
    {
        LOG_ERR("Failed loading server cert");
        return -EIO;
    }

    /* We're pulling private fields here, which isn't great.
     * Unfortunately, there's no straight forward way to export the
     * raw public key out of the mbedtls_pk structure. This works fine,
     * but may change in a new version of Mbed TLS.
     */
    uint8_t *pk = parsed_cert.pk.MBEDTLS_PRIVATE(pub_raw);
    size_t pk_len = parsed_cert.pk.MBEDTLS_PRIVATE(pub_raw_len);
    if (pk_len > sizeof(server_cert.pubkey.data))
    {
        LOG_ERR("Unexpected server public key size: %u", pk_len);
        goto exit;
    }

    if (parsed_cert.serial.len > sizeof(server_cert.serial.data))
    {
        LOG_ERR("Unexpected server certificate serial number size: %u", parsed_cert.serial.len);
        goto exit;
    }

    if (IS_ENABLED(CONFIG_POUCH_VALIDATE_SERVER_CERT))
    {
        err = authenticate_server_cert(&parsed_cert);
        if (err)
        {
            goto exit;
        }
    }

    memcpy(server_cert.pubkey.data, pk, pk_len);
    server_cert.pubkey.len = pk_len;

    memcpy(server_cert.serial.data, parsed_cert.serial.p, parsed_cert.serial.len);
    server_cert.serial.len = parsed_cert.serial.len;

    LOG_DBG("Server key stored");

exit:
    mbedtls_x509_crt_free(&parsed_cert);
    return err;
}

const uint8_t *cert_ref_get(void)
{
    if (!cert_is_valid(&device.certificate))
    {
        return NULL;
    }

    return device.ref;
}

void cert_server_key_get(struct pubkey *out)
{
    *out = server_cert.pubkey;
}

bool cert_has_server_info(void)
{
    return server_cert.pubkey.len != 0;
}

/*
 * Transport API
 */

int pouch_server_certificate_set(const struct pouch_cert *cert)
{
    return cert_server_set(cert);
}

ssize_t pouch_server_certificate_serial_get(uint8_t *serial, size_t len)
{
    if (len < server_cert.serial.len)
    {
        return -EINVAL;
    }

    memcpy(serial, server_cert.serial.data, server_cert.serial.len);

    return server_cert.serial.len;
}

int pouch_device_certificate_ref_get(uint8_t *cert_ref, size_t len)
{
    if (!cert_is_valid(&device.certificate) || len > sizeof(device.ref))
    {
        return -EINVAL;
    }

    memcpy(cert_ref, device.ref, len);

    return 0;
}

int pouch_device_certificate_get(struct pouch_cert *out)
{
    if (!cert_is_valid(&device.certificate))
    {
        return -ENOENT;
    }

    *out = device.certificate;

    return 0;
}
