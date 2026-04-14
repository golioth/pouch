/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>

#include <errno.h>
#include <pouch/downlink.h>
#include <pouch/port.h>
#include <pouch/types.h>
#include <pouch/transport/downlink.h>
#include "cddl/header_decode.h"

#include "block.h"
#include <pouch/blockbuf.h>
#include "buf.h"
#include "crypto.h"
#include "downlink.h"
#include "entry.h"

POUCH_LOG_REGISTER(downlink, CONFIG_POUCH_LOG_LEVEL);

static struct pouch_buf *encrypted;
static bool pouch_header;

static struct
{
    pouch_buf_queue_t queue;
    pouch_work_t work;
    pouch_work_q_t *work_queue;
} decrypt;

static void decrypt_blocks(pouch_work_t *work);

int downlink_init(pouch_work_q_t *pouch_work_queue)
{
    buf_queue_init(&decrypt.queue);
    pouch_work_init(&decrypt.work, decrypt_blocks);
    decrypt.work_queue = pouch_work_queue;

    return 0;
}

static void decrypt_blocks(pouch_work_t *work)
{
    struct pouch_buf *encrypted_block;
    struct pouch_buf *decrypted_block = blockbuf_alloc(POUCH_FOREVER);
    if (decrypted_block == NULL)
    {
        POUCH_LOG_ERR("Failed to allocate decrypt block");
        return;
    }

    while ((encrypted_block = buf_queue_get(&decrypt.queue)) != NULL)
    {
        // Reset the target buffer
        buf_restore(decrypted_block, POUCH_BUF_STATE_INITIAL);

        /* Decrypt this block */
        int err = crypto_decrypt_block(encrypted_block, decrypted_block);

        /* Encrypted block was consumed; free buffer no matter the outcome */
        /* buffers were allocated then enqueued in pouch_downlink_push() */
        buf_free(encrypted_block);

        /* Test to see if decrypt buffer contains valid data */
        if (err)
        {
            POUCH_LOG_ERR("Failed to decrypt block: %d", err);
            // TODO: Abort the downlink
            break;
        }

        pouch_downlink_block_push(decrypted_block);

        pouch_yield();  // let other threads run
    }

    blockbuf_free(decrypted_block);
}

static int block_downlink_push(struct pouch_buf *pouch_buf)
{
    buf_queue_submit(&decrypt.queue, pouch_buf);
    return pouch_work_submit_to_queue(decrypt.work_queue, &decrypt.work);
}

void pouch_downlink_start(void)
{
    POUCH_LOG_DBG("Pouch downlink start");

    pouch_header = false;

    encrypted = buf_alloc(MAX_CIPHERTEXT_BLOCK_SIZE);
    if (!encrypted)
    {
        POUCH_LOG_ERR("Failed to allocate pouch buf");
        return;
    }
}

static int pouch_downlink_parse_header(struct pouch_bufview *v, size_t *header_len)
{
    const uint8_t *header_raw = pouch_bufview_read(v, 0);
    struct pouch_header header;
    int ret;

    ret = cbor_decode_pouch_header(header_raw, pouch_bufview_available(v), &header, header_len);
    if (ret != ZCBOR_SUCCESS)
    {
        POUCH_LOG_DBG("Failed to decode pouch header: %d", ret);
        return -EIO;
    }

    POUCH_LOG_HEXDUMP(header_raw, *header_len, "pouch header raw");

    POUCH_LOG_DBG("Header version %d", (int) header.version);
    POUCH_LOG_DBG("Encryption type %s",
                  (int) header.encryption_info_m.Union_choice
                          == encryption_info_union_plaintext_info_m_c
                      ? "Plaintext"
                      : "SAEAD");
    POUCH_LOG_DBG("Payload len %d", (int) *header_len);

    int err = crypto_downlink_start(&header.encryption_info_m);
    if (err)
    {
        POUCH_LOG_ERR("Invalid header: %d", err);
        return err;
    }

    return 0;
}

int pouch_downlink_push(const void *buf, size_t buf_len)
{
    const uint8_t *buf_p = buf;

    POUCH_LOG_HEXDUMP(buf, buf_len, "Pouch downlink push: ");

    while (buf_len)
    {
        if (!encrypted)
        {
            POUCH_LOG_WRN("No pouch_buf allocated");
            return -ENOMEM;
        }

        if (buf_size_get(encrypted) >= MAX_CIPHERTEXT_BLOCK_SIZE)
        {
            POUCH_LOG_ERR("No more space for pouch header");
            return -ENOMEM;
        }

        size_t buf_written = MIN(buf_len, MAX_CIPHERTEXT_BLOCK_SIZE - buf_size_get(encrypted));
        buf_write(encrypted, buf_p, buf_written);

        buf_p += buf_written;
        buf_len -= buf_written;

        struct pouch_bufview v;
        pouch_bufview_init(&v, encrypted);

        if (!pouch_header)
        {
            size_t header_len = 0;
            int err = pouch_downlink_parse_header(&v, &header_len);
            if (err)
            {
                /* Match previous behavior but needs more differentiation. Future work tracked here:
                 * https://github.com/golioth/firmware-issue-tracker/issues/924
                 */
                return 0;
            }

            pouch_header = true;

            /* Align first block with encrypted start */
            buf_trim_start(encrypted, header_len);

            POUCH_LOG_HEXDUMP(pouch_bufview_read(&v, 0), pouch_bufview_available(&v), "remaining");
        }

        if (pouch_bufview_available(&v) > sizeof(uint16_t))
        {
            uint16_t block_size = pouch_bufview_read_be16(&v);

            if (block_size > MAX_BLOCK_SIZE_FIELD_VALUE)
            {
                POUCH_LOG_ERR("Block size %u is bigger than supported %u",
                              (unsigned int) block_size,
                              (unsigned int) (MAX_BLOCK_SIZE_FIELD_VALUE));
                return -ENOMEM;
            }

            if (pouch_bufview_available(&v) >= block_size)
            {
                POUCH_LOG_DBG("Block ready %d available %d",
                              (int) block_size,
                              (int) pouch_bufview_available(&v));

                struct pouch_buf *encrypted_block = encrypted;

                encrypted = buf_alloc(MAX_CIPHERTEXT_BLOCK_SIZE);
                if (!encrypted)
                {
                    POUCH_LOG_ERR("Failed to allocate pouch buf");
                    return -ENOMEM;
                }

                if (pouch_bufview_available(&v) > block_size)
                {
                    const uint8_t *remaining = pouch_bufview_read(&v, 0);
                    remaining += block_size;
                    size_t remaining_len = pouch_bufview_available(&v) - block_size;

                    POUCH_LOG_HEXDUMP(remaining, remaining_len, "remaining");

                    buf_write(encrypted, remaining, remaining_len);
                    buf_trim_end(encrypted_block, remaining_len);
                }

                /* buffers pushed to queue are freed in decrypt_blocks(). */
                int err = block_downlink_push(encrypted_block);
                if (0 > err)
                {
                    POUCH_LOG_ERR("Failed to enqueue block: %d", err);
                    return err;
                }
            }
        }
    }

    return 0;
}

void pouch_downlink_finish(void)
{
    buf_free(encrypted);
    encrypted = NULL;
}
