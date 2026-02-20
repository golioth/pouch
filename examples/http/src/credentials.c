/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdlib.h>
#include <zephyr/fs/fs.h>
#include <pouch/pouch.h>
#include <mbedtls/pk.h>
#include <zephyr/net/tls_credentials.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(credentials);

#define CERT_DIR CONFIG_EXAMPLE_CREDENTIALS_DIR
#define TLS_CA_FILE CERT_DIR "/ca.der"
#define GW_CERT_FILE CERT_DIR "/" CONFIG_EXAMPLE_HTTP_GW_DEVICE_CRT_FILENAME
#define GW_KEY_FILE CERT_DIR "/" CONFIG_EXAMPLE_HTTP_GW_DEVICE_KEY_FILENAME
#define CERT_FILE CERT_DIR "/crt.der"
#define KEY_FILE CERT_DIR "/key.der"

struct mutual_tls_pki {
    uint8_t *ca_cert;
    ssize_t ca_cert_len;
    uint8_t *gw_crt;
    ssize_t gw_crt_len;
    uint8_t *gw_key;
    ssize_t gw_key_len;
} _m_tls_ctx;

#ifndef CONFIG_POUCH_HTTP_TLS_LOAD_CA_FROM_FILESYSTEM
static const uint8_t tls_ca_crt[] = {
#include "pouch-ca-crt.inc"
};
#endif // CONFIG_POUCH_HTTP_TLS_LOAD_CA_FROM_FILESYSTEM

static int psa_rng_for_mbedtls(void *p_rng, unsigned char *output, size_t output_len)
{
    return psa_generate_random(output, output_len);
}

/** Load the raw private key data into PSA */
static psa_key_id_t import_raw_pk(const uint8_t *private_key, size_t size)
{
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    int err = mbedtls_pk_parse_key(&pk, private_key, size, NULL, 0, psa_rng_for_mbedtls, NULL);
    if (err)
    {
        LOG_ERR("Failed to parse key: -0x%x", -err);
        return PSA_KEY_ID_NULL;
    }

    psa_key_attributes_t attrs = PSA_KEY_ATTRIBUTES_INIT;

    psa_set_key_algorithm(&attrs, PSA_ALG_ECDH);
    psa_set_key_type(&attrs, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_usage_flags(&attrs, PSA_KEY_USAGE_DERIVE);

    psa_key_id_t key_id;
    err = mbedtls_pk_import_into_psa(&pk, &attrs, &key_id);
    if (err)
    {
        LOG_ERR("Failed to import private key: -0x%x", -err);
        return PSA_KEY_ID_NULL;
    }

    return key_id;
}

static void ensure_credentials_dir(void)
{
    struct fs_dirent dirent;

    int err = fs_stat(CERT_DIR, &dirent);
    if (err == -ENOENT)
    {
        err = fs_mkdir(CERT_DIR);
    }

    if (err)
    {
        LOG_ERR("Failed to create credentials dir: %d", err);
    }
}

static ssize_t get_file_size(const char *path)
{
    struct fs_dirent dirent;

    int err = fs_stat(path, &dirent);

    if (err < 0)
    {
        return err;
    }
    if (dirent.type != FS_DIR_ENTRY_FILE)
    {
        return -EISDIR;
    }

    return dirent.size;
}

static ssize_t read_file(const char *path, uint8_t **out)
{
    // Ensure that the credentials directory exists, so the user doesn't have to
    ensure_credentials_dir();

    ssize_t size = get_file_size(path);
    if (size <= 0)
    {
        return size;
    }

    uint8_t *buf = malloc(size);
    if (buf == NULL)
    {
        return -ENOMEM;
    }

    struct fs_file_t file;
    fs_file_t_init(&file);

    int err = fs_open(&file, path, FS_O_READ);
    if (err)
    {
        LOG_ERR("Could not open %s, err: %d", path, err);
        free(buf);
        return err;
    }

    size = fs_read(&file, buf, size);
    if (size < 0)
    {
        LOG_ERR("Could not read %s, err: %d", path, size);
        free(buf);
        goto finish;
    }

    LOG_DBG("Read %d bytes from %s", size, path);

    *out = buf;

finish:
    fs_close(&file);
    return size;
}

psa_key_id_t load_private_key(void)
{
    uint8_t *buf;
    ssize_t size = read_file(KEY_FILE, &buf);
    if (size < 0)
    {
        return PSA_KEY_ID_NULL;
    }

    psa_key_id_t key_id = import_raw_pk(buf, size);
    free(buf);
    return key_id;
}

int load_certificate(struct pouch_cert *cert)
{
    uint8_t *buf;
    ssize_t size = read_file(CERT_FILE, &buf);
    if (size < 0)
    {
        return size;
    }

    cert->buffer = buf;
    cert->size = size;

    LOG_DBG("Read certificate (%d bytes)", size);

    return 0;
}

void free_certificate(struct pouch_cert *cert)
{
    free((void *) cert->buffer);
}

ssize_t load_file_to_tls(char *fn, sec_tag_t sec_tag, enum tls_credential_type type, uint8_t **buf)
{
    ssize_t size = read_file(fn, buf);
    if (size < 0)
    {
        return size;
    }

    int err = tls_credential_add(sec_tag,
                                 type,
                                 *buf,
                                 size);
    return err;
}

int load_http_server_ca(sec_tag_t sec_tag)
{
#ifdef CONFIG_POUCH_HTTP_TLS_LOAD_CA_FROM_FILESYSTEM
    if (NULL != _m_tls_ctx.ca_cert)
    {
        LOG_ERR("Free existing ca cert before loading");
        return -EEXIST;
    }

    _m_tls_ctx.ca_cert_len = load_file_to_tls(TLS_CA_FILE,
                                              sec_tag,
                                              TLS_CREDENTIAL_CA_CERTIFICATE,
                                              &_m_tls_ctx.ca_cert);
    return _m_tls_ctx.gw_key_len;
#else
    return tls_credential_add(sec_tag,
                              TLS_CREDENTIAL_CA_CERTIFICATE,
                              tls_ca_crt,
                              sizeof(tls_ca_crt));
#endif
}

int load_http_gw_device_crt(sec_tag_t sec_tag)
{
    if (NULL != _m_tls_ctx.gw_crt)
    {
        LOG_ERR("Free existing gw cert before loading");
        return -EEXIST;
    }

    _m_tls_ctx.gw_crt_len = load_file_to_tls(GW_CERT_FILE,
                                             sec_tag,
                                             TLS_CREDENTIAL_PUBLIC_CERTIFICATE,
                                             &_m_tls_ctx.gw_crt);
    return _m_tls_ctx.gw_key_len;
}

int load_http_gw_device_key(sec_tag_t sec_tag)
{
    if (NULL != _m_tls_ctx.gw_key)
    {
        LOG_ERR("Free existing gw key before loading");
        return -EEXIST;
    }

    _m_tls_ctx.gw_key_len =
        load_file_to_tls(GW_KEY_FILE, sec_tag, TLS_CREDENTIAL_PRIVATE_KEY, &_m_tls_ctx.gw_key);
    return _m_tls_ctx.gw_key_len;
}

void free_http_certs(void)
{
#ifdef CONFIG_POUCH_HTTP_TLS_LOAD_CA_FROM_FILESYSTEM
    free(_m_tls_ctx.ca_cert);
#endif
    _m_tls_ctx.ca_cert = NULL;
    _m_tls_ctx.ca_cert_len = 0;

    free(_m_tls_ctx.gw_crt);
    _m_tls_ctx.gw_crt = NULL;
    _m_tls_ctx.gw_crt_len = 0;

    free(_m_tls_ctx.gw_key);
    _m_tls_ctx.gw_key = NULL;
    _m_tls_ctx.gw_key_len = 0;
}
