/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <zephyr/sys/byteorder.h>

#include <pouch/downlink.h>
#include <pouch/types.h>
#include <pouch/transport/downlink.h>
#include "cddl/header_decode.h"

#include "block.h"
#include "crypto.h"
#include "downlink.h"
#include "entry.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(downlink);

static struct pouch_buf *pouch_buf;
static bool pouch_header;

static struct
{
    pouch_buf_queue_t queue;
    struct k_work work;
} decrypt;

static struct
{
    pouch_buf_queue_t queue;
    struct k_work work;
} consume;

static void decrypt_blocks(struct k_work *work);
static void consume_blocks(struct k_work *work);

void downlink_init(void)
{
    buf_queue_init(&decrypt.queue);
    k_work_init(&decrypt.work, decrypt_blocks);

    buf_queue_init(&consume.queue);
    k_work_init(&consume.work, consume_blocks);
}

static void consume_blocks(struct k_work *work)
{
    struct pouch_buf *pouch_buf = buf_queue_get(&consume.queue);
    if (!pouch_buf)
    {
        return;
    }

    pouch_downlink_block_push(pouch_buf);

    buf_free(pouch_buf);

    if (!buf_queue_is_empty(&consume.queue))
    {
        k_work_submit(work);
    }
}

static void decrypt_blocks(struct k_work *work)
{
    struct pouch_buf *decrypted = crypto_decrypt_block(buf_queue_get(&decrypt.queue));
    if (!decrypted)
    {
        return;
    }

    buf_queue_submit(&consume.queue, decrypted);
    k_work_submit(&consume.work);

    if (!buf_queue_is_empty(&decrypt.queue))
    {
        k_work_submit(work);
    }
}

static void block_downlink_push(struct pouch_buf *pouch_buf)
{
    buf_queue_submit(&decrypt.queue, pouch_buf);
    k_work_submit(&decrypt.work);
}

void pouch_downlink_start(void)
{
    LOG_DBG("Pouch downlink start");

    pouch_header = false;

    pouch_buf = buf_alloc(MAX_BLOCK_SIZE + CONFIG_POUCH_AUTH_TAG_LEN);
    if (!pouch_buf)
    {
        LOG_ERR("Failed to allocate pouch buf");
        return;
    }
}

static size_t pouch_downlink_parse_header(struct pouch_bufview *v)
{
    const uint8_t *header_raw = pouch_bufview_read(v, 0);
    struct pouch_header header;
    size_t header_len;
    int ret;

    ret = cbor_decode_pouch_header(header_raw, pouch_bufview_available(v), &header, &header_len);
    if (ret != ZCBOR_SUCCESS)
    {
        LOG_DBG("Failed to decode pouch header: %d", ret);
        return 0;
    }

    LOG_HEXDUMP_DBG(header_raw, header_len, "pouch header raw");

    LOG_DBG("Header version %d", (int) header.version);
    LOG_DBG("Encryption type %s",
            (int) header.encryption_info_m.Union_choice == encryption_info_union_plaintext_info_m_c
                ? "Plaintext"
                : "SAEAD");
    LOG_DBG("Payload len %d", (int) header_len);

    int err = crypto_downlink_start(&header.encryption_info_m);
    if (err)
    {
        LOG_ERR("Invalid header: %d", err);
        return err;
    }

    return header_len;
}

void pouch_downlink_push(const void *buf, size_t buf_len)
{
    const uint8_t *buf_p = buf;

    LOG_HEXDUMP_DBG(buf, buf_len, "Pouch downlink push: ");

    while (buf_len)
    {
        if (!pouch_buf)
        {
            LOG_WRN("No pouch_buf allocated");
            return;
        }

        if (buf_size_get(pouch_buf) >= MAX_BLOCK_SIZE + CONFIG_POUCH_AUTH_TAG_LEN)
        {
            LOG_ERR("No more space for pouch header");
            return;
        }

        size_t buf_written =
            MIN(buf_len, MAX_BLOCK_SIZE + CONFIG_POUCH_AUTH_TAG_LEN - buf_size_get(pouch_buf));
        buf_write(pouch_buf, buf_p, buf_written);

        buf_p += buf_written;
        buf_len -= buf_written;

        struct pouch_bufview v;
        pouch_bufview_init(&v, pouch_buf);

        if (!pouch_header)
        {
            size_t header_len = pouch_downlink_parse_header(&v);

            if (!header_len)
            {
                return;
            }

            pouch_header = true;

            /* Align first block with pouch_buf start */
            buf_trim_start(pouch_buf, header_len);

            LOG_HEXDUMP_DBG(pouch_bufview_read(&v, 0), pouch_bufview_available(&v), "remaining");
        }

        if (pouch_bufview_available(&v) > sizeof(uint16_t))
        {
            uint16_t block_size = pouch_bufview_read_be16(&v);

            if (block_size >= MAX_BLOCK_SIZE + CONFIG_POUCH_AUTH_TAG_LEN)
            {
                LOG_ERR("Block size %u is bigger than supported %u",
                        (unsigned int) block_size,
                        (unsigned int) (MAX_BLOCK_SIZE + CONFIG_POUCH_AUTH_TAG_LEN));
                return;
            }

            if (pouch_bufview_available(&v) >= block_size)
            {
                LOG_DBG("Block ready %d available %d",
                        (int) block_size,
                        (int) pouch_bufview_available(&v));

                struct pouch_buf *pouch_buf_to_send = pouch_buf;

                pouch_buf = buf_alloc(MAX_BLOCK_SIZE + CONFIG_POUCH_AUTH_TAG_LEN);
                if (!pouch_buf)
                {
                    LOG_ERR("Failed to allocate pouch buf");
                }

                if (pouch_buf && pouch_bufview_available(&v) > block_size)
                {
                    const uint8_t *remaining = pouch_bufview_read(&v, 0);
                    remaining += block_size;
                    size_t remaining_len = pouch_bufview_available(&v) - block_size;

                    LOG_HEXDUMP_DBG(remaining, remaining_len, "remaining");

                    buf_write(pouch_buf, remaining, remaining_len);
                    buf_trim_end(pouch_buf_to_send, remaining_len);
                }

                block_downlink_push(pouch_buf_to_send);
            }
        }
    }
}

void pouch_downlink_finish(void) {}
