/*
 * Copyright (c) 2025 Golioth
 */

#include <stdlib.h>

#include <psa/crypto.h>

#include <pouch/types.h>
#include <pouch/transport/downlink.h>
#include "cddl/header_decode.h"

#include "block.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(downlink, LOG_LEVEL_DBG);

static psa_hash_operation_t hash;
static uint32_t byte_counter = 0;

static struct pouch_block pouch_block;
static struct pouch_buf *pouch_buf;
static pouch_buf_state_t pouch_buf_initial_state;
static uint8_t *pouch_data;
static bool pouch_header;

static int downlink_init(void)
{
    pouch_buf = buf_alloc(CONFIG_POUCH_BLOCK_SIZE);
    if (!pouch_buf)
    {
        LOG_ERR("Failed to allocate pouch buf");
        return -ENOMEM;
    }

    pouch_data = buf_next(pouch_buf);
    pouch_buf_initial_state = buf_state_get(pouch_buf);

    return 0;
}

SYS_INIT(downlink_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

__weak void downlink_received(const char *path,
                              uint16_t content_type,
                              const void *data,
                              size_t len,
                              size_t offset,
                              bool is_last)
{
    LOG_INF("Entry path: %s", path);
    LOG_INF("Entry content_type: %u", content_type);
    LOG_INF("Entry data offset: %zu", offset);
    LOG_INF("Entry is_last: %d", (int) is_last);
    LOG_HEXDUMP_INF(data, len, "Entry data");
}

void pouch_downlink_start(void)
{
    LOG_DBG("Pouch downlink start");

    pouch_header = false;

    byte_counter = 0;
    buf_restore(pouch_buf, pouch_buf_initial_state);

    hash = psa_hash_operation_init();
    psa_hash_setup(&hash, PSA_ALG_SHA_256);

    block_downlink_start(&pouch_block);
}

void pouch_downlink_push(const void *buf, size_t buf_len)
{
    struct pouch_header header;
    size_t payload_len;
    int ret;

    LOG_HEXDUMP_DBG(buf, buf_len, "Pouch downlink push: ");

    if (!pouch_header)
    {
        if (byte_counter >= CONFIG_POUCH_BLOCK_SIZE)
        {
            LOG_ERR("No more space for pouch header");
            return;
        }

        buf_write(pouch_buf, buf, MIN(buf_len, CONFIG_POUCH_BLOCK_SIZE - byte_counter));

        ret = cbor_decode_pouch_header(pouch_data, buf_size_get(pouch_buf), &header, &payload_len);
        if (ret != ZCBOR_SUCCESS)
        {
            LOG_ERR("Failed to decode pouch header: %d", ret);
        }
        else
        {
            pouch_header = true;

            LOG_INF("Header version %d", (int) header.version);
            LOG_INF("Payload len %d", (int) payload_len);

            LOG_HEXDUMP_DBG(pouch_data, payload_len, "pouch header raw");

            const uint8_t *payload = pouch_data;
            payload += payload_len;

            LOG_HEXDUMP_DBG(payload, buf_next(pouch_buf) - payload, "remaining");

            block_downlink_push(&pouch_block, payload, buf_next(pouch_buf) - payload);
        }
    }
    else
    {
        block_downlink_push(&pouch_block, buf, buf_len);
    }

    byte_counter += buf_len;

    psa_hash_update(&hash, buf, buf_len);
}

void pouch_downlink_finish(void)
{
    uint8_t hash_out[32];
    size_t hash_length;
    psa_hash_finish(&hash, hash_out, 32, &hash_length);
    LOG_HEXDUMP_DBG(hash_out, sizeof(hash_out), "Pouch downlink finish: ");

    block_downlink_finish(&pouch_block);

    LOG_DBG("Total bytes: %d", byte_counter);
}
