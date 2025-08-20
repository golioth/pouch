/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdlib.h>
#include <zephyr/fs/fs.h>
#include <pouch/pouch.h>
#include <mbedtls/pk.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(credentials);

#define CERT_DIR CONFIG_EXAMPLE_CREDENTIALS_DIR
#define CERT_FILE CERT_DIR "/crt.der"
#define KEY_FILE CERT_DIR "/key.der"

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
