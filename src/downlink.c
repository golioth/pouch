/*
 * Copyright (c) 2025 Golioth
 */

#include <stdlib.h>

#include <psa/crypto.h>

#include <pouch/transport/downlink.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(downlink, LOG_LEVEL_DBG);

static psa_hash_operation_t hash;
uint32_t byte_counter = 0;

void pouch_downlink_start(void)
{
    LOG_DBG("Pouch downlink start");

    byte_counter = 0;

    hash = psa_hash_operation_init();
    psa_hash_setup(&hash, PSA_ALG_SHA_256);
}

void pouch_downlink_push(const void *buf, size_t buf_len)
{
    LOG_HEXDUMP_DBG(buf, buf_len, "Pouch downlink push: ");

    byte_counter += buf_len;

    psa_hash_update(&hash, buf, buf_len);
}

void pouch_downlink_finish(void)
{
    uint8_t hash_out[32];
    size_t hash_length;
    psa_hash_finish(&hash, hash_out, 32, &hash_length);
    LOG_HEXDUMP_DBG(hash_out, sizeof(hash_out), "Pouch downlink finish: ");
    LOG_DBG("Total bytes: %d", byte_counter);
}
