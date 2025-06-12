/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#include "crypto.h"
#include "cert.h"
#include "saead/uplink.h"
#include "saead/downlink.h"
#include <psa/crypto.h>
#include <zephyr/sys/byteorder.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/base64.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(crypto, LOG_LEVEL_DBG);

#define PUBKEY_LEN PSA_EXPORT_PUBLIC_KEY_MAX_SIZE

#if defined(CONFIG_POUCH_ENCRYPTION_CHACHA20_POLY1305)
#define POUCH_PSA_ENCRYPTION_ALG PSA_ALG_CHACHA20_POLY1305
#elif defined(CONFIG_POUCH_ENCRYPTION_AES_GCM)
#define POUCH_PSA_ENCRYPTION_ALG PSA_ALG_GCM
#else
#error "Unsupported encryption type"
#endif

static psa_key_id_t private_key;

static bool is_ready(void)
{
    return cert_ref_get() != NULL && cert_has_server_info();
}

int crypto_init(const struct pouch_config *config)
{
    int err;

    if (config->encryption.saead.private_key == PSA_KEY_ID_NULL)
    {
        return -EINVAL;
    }

    err = cert_device_set(&config->encryption.saead.certificate);
    if (err)
    {
        return err;
    }

    private_key = config->encryption.saead.private_key;

    return 0;
}

int crypto_session_start(void)
{
    if (!is_ready())
    {
        LOG_ERR("Not ready for a session");
        return -EBUSY;
    }

    return uplink_session_start(POUCH_PSA_ENCRYPTION_ALG, private_key);
}

void crypto_session_end(void)
{
    downlink_session_end();
    uplink_session_end();
}

pouch_id_t crypto_pouch_start(void)
{
    return uplink_pouch_start();
}

int crypto_session_info_get(struct session_info *session_info)
{
    return uplink_session_info_get(session_info);
}

struct pouch_buf *crypto_encrypt_block(struct pouch_buf *block)
{
    return uplink_encrypt_block(block);
}
