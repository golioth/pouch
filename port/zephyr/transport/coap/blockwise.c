/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/net/coap.h>
#include <zephyr/sys/util.h>

#include "coap_internal.h"

LOG_MODULE_DECLARE(pouch_coap, CONFIG_POUCH_COAP_CLIENT_LOG_LEVEL);

/*--------------------------------------------------
 * Block helpers
 *------------------------------------------------*/

static enum coap_block_size pouch_coap_block_size(void)
{
    return coap_bytes_to_block_size(CONFIG_POUCH_COAP_BLOCK_SIZE);
}

static bool pouch_coap_code_is_success(uint8_t code)
{
    return (code >> 5) == 2;
}

/*
 * Log and return -EIO when the server responds with a
 * non-success CoAP response code.
 */
static int pouch_coap_check_response_code(const struct coap_packet *resp, const char *path)
{
    uint8_t code = coap_header_get_code(resp);

    if (!pouch_coap_code_is_success(code))
    {
        LOG_ERR("%s failed with code %d.%02d", path, code >> 5, code & 0x1f);
        return -EIO;
    }

    return 0;
}

/* Append a payload chunk to a CoAP request packet. */
static int pouch_coap_append_payload_chunk(struct coap_packet *req,
                                           const uint8_t *payload,
                                           size_t offset,
                                           size_t len)
{
    int ret;

    if (len == 0)
    {
        return 0;
    }

    ret = coap_packet_append_payload_marker(req);
    if (ret < 0)
    {
        return ret;
    }

    return coap_packet_append_payload(req, payload + offset, len);
}

/*
 * Update block context from a response and report whether more
 * blocks are expected.
 */
static int pouch_coap_block_update(const struct coap_packet *resp,
                                   struct coap_block_context *blk_ctx,
                                   bool *has_more)
{
    int ret = coap_update_from_block(resp, blk_ctx);

    if (ret < 0)
    {
        LOG_ERR("Failed to update block context: %d", ret);
        return ret;
    }

    *has_more = coap_next_block(resp, blk_ctx) != 0;
    return 0;
}

/*
 * Build a Block2 request, send it, and validate the response code.
 *
 * This is the common request cycle used by blockwise GET downloads
 * and Block2 continuation after a POST.
 */
static int pouch_coap_block2_request(struct coap_packet *resp,
                                     const uint8_t *token,
                                     uint8_t token_len,
                                     uint8_t method,
                                     const char *path,
                                     struct coap_block_context *blk_ctx)
{
    struct coap_packet req;
    int ret;

    ret = pouch_coap_build_request(&req, method, token, token_len, coap_next_id(), path, -1);
    if (ret < 0)
    {
        LOG_ERR("Failed to build Block2 request: %d", ret);
        return ret;
    }

    ret = coap_append_block2_option(&req, blk_ctx);
    if (ret < 0)
    {
        LOG_ERR("Failed to append Block2 option: %d", ret);
        return ret;
    }

    ret = pouch_coap_send_and_recv(&req, resp, token, token_len);
    if (ret < 0)
    {
        return ret;
    }

    return pouch_coap_check_response_code(resp, path);
}

/*
 * Negotiate a potentially smaller block size from the server's
 * Block1 option in its response. Returns the (possibly updated)
 * block size in bytes.
 */
static uint16_t pouch_coap_negotiate_block1_size(struct coap_block_context *blk_ctx,
                                                 const struct coap_packet *resp,
                                                 uint16_t current_block_bytes)
{
    int block1_opt = coap_get_option_int(resp, COAP_OPTION_BLOCK1);

    if (block1_opt < 0)
    {
        return current_block_bytes;
    }

    enum coap_block_size server_bs = block1_opt & 0x07;

    if (server_bs < blk_ctx->block_size)
    {
        blk_ctx->block_size = server_bs;
        return coap_block_size_to_bytes(server_bs);
    }

    return current_block_bytes;
}

/*
 * Fetch remaining Block2 response blocks after the initial
 * response has been processed.
 *
 * Sends POST requests with Block2 option (per RFC 7959) to
 * retrieve subsequent blocks and delivers each chunk to
 * @p resp_cb.
 */
static int pouch_coap_block2_fetch_remaining(const char *path,
                                             const uint8_t *token,
                                             uint8_t token_len,
                                             struct coap_block_context *blk_ctx,
                                             block2_payload_cb_t resp_cb,
                                             void *user_data)
{
    struct coap_packet resp;

    while (true)
    {
        uint16_t resp_len = 0;
        const uint8_t *resp_data;
        bool has_more;
        int ret;

        ret = pouch_coap_block2_request(&resp, token, token_len, COAP_METHOD_POST, path, blk_ctx);
        if (ret < 0)
        {
            return ret;
        }

        resp_data = coap_packet_get_payload(&resp, &resp_len);

        ret = pouch_coap_block_update(&resp, blk_ctx, &has_more);
        if (ret < 0)
        {
            return ret;
        }

        if (resp_len > 0 || !has_more)
        {
            ret = resp_cb(resp_data, resp_len, false, !has_more, user_data);
            if (ret < 0)
            {
                return ret;
            }
        }

        if (!has_more)
        {
            break;
        }
    }

    return 0;
}

/*--------------------------------------------------
 * Blockwise transfers
 *------------------------------------------------*/

/*
 * Perform a blockwise GET and collect the full response payload
 * into a caller-provided buffer.
 *
 * A single CoAP token is reused for all block requests so the
 * server can correlate them. A new message ID is used per block.
 */
int pouch_coap_blockwise_get(const char *path, uint8_t *buf, size_t buf_size, size_t *out_len)
{
    struct coap_block_context blk_ctx;
    struct coap_packet resp;
    uint8_t token[COAP_TOKEN_MAX_LEN];
    size_t total_received = 0;
    int ret;

    memcpy(token, coap_next_token(), COAP_TOKEN_MAX_LEN);
    coap_block_transfer_init(&blk_ctx, pouch_coap_block_size(), 0);

    while (true)
    {
        uint16_t payload_len = 0;
        const uint8_t *payload;
        bool has_more;

        ret = pouch_coap_block2_request(&resp,
                                        token,
                                        COAP_TOKEN_MAX_LEN,
                                        COAP_METHOD_GET,
                                        path,
                                        &blk_ctx);
        if (ret < 0)
        {
            return ret;
        }

        payload = coap_packet_get_payload(&resp, &payload_len);

        if (payload_len > 0)
        {
            if (total_received + payload_len > buf_size)
            {
                LOG_ERR(
                    "Response too large for buffer "
                    "(%zu + %u > %zu)",
                    total_received,
                    payload_len,
                    buf_size);
                return -ENOMEM;
            }

            memcpy(buf + total_received, payload, payload_len);
            total_received += payload_len;
        }

        ret = pouch_coap_block_update(&resp, &blk_ctx, &has_more);
        if (ret < 0)
        {
            return ret;
        }

        if (!has_more)
        {
            break;
        }
    }

    *out_len = total_received;
    return 0;
}

/*
 * Perform a POST with optional Block1 (upload) and Block2
 * (download).
 *
 * If the payload fits in a single block it is sent in one request.
 * Otherwise Block1 is used to chunk the upload.
 *
 * If @p resp_cb is non-NULL the response payload is streamed
 * through it block-by-block (Block2).
 */
int pouch_coap_blockwise_post(const char *path,
                              const uint8_t *payload,
                              size_t payload_len,
                              block2_payload_cb_t resp_cb,
                              void *user_data)
{
    struct coap_block_context send_blk_ctx;
    struct coap_block_context recv_blk_ctx;
    struct coap_packet req;
    struct coap_packet resp;
    uint8_t token[COAP_TOKEN_MAX_LEN];
    uint16_t block_size_bytes;
    bool need_block1;
    bool upload_done = false;
    int ret;

    memcpy(token, coap_next_token(), COAP_TOKEN_MAX_LEN);

    block_size_bytes = coap_block_size_to_bytes(pouch_coap_block_size());
    need_block1 = (payload_len > block_size_bytes);

    coap_block_transfer_init(&send_blk_ctx, pouch_coap_block_size(), payload_len);
    coap_block_transfer_init(&recv_blk_ctx, pouch_coap_block_size(), 0);

    while (!upload_done)
    {
        size_t chunk_offset = need_block1 ? send_blk_ctx.current : 0;
        size_t chunk_len = MIN(payload_len - chunk_offset, block_size_bytes);

        ret = pouch_coap_build_request(&req,
                                       COAP_METHOD_POST,
                                       token,
                                       COAP_TOKEN_MAX_LEN,
                                       coap_next_id(),
                                       path,
                                       COAP_CONTENT_FORMAT_APP_OCTET_STREAM);
        if (ret < 0)
        {
            LOG_ERR("Failed to build POST request: %d", ret);
            return ret;
        }

        if (need_block1)
        {
            ret = coap_append_block1_option(&req, &send_blk_ctx);
            if (ret < 0)
            {
                LOG_ERR("Failed to append Block1: %d", ret);
                return ret;
            }
        }

        /* Negotiate Block2 size in the first request. */
        if (resp_cb && send_blk_ctx.current == 0)
        {
            ret = coap_append_block2_option(&req, &recv_blk_ctx);
            if (ret < 0)
            {
                LOG_ERR("Failed to append Block2: %d", ret);
                return ret;
            }
        }

        ret = pouch_coap_append_payload_chunk(&req, payload, chunk_offset, chunk_len);
        if (ret < 0)
        {
            return ret;
        }

        ret = pouch_coap_send_and_recv(&req, &resp, token, COAP_TOKEN_MAX_LEN);
        if (ret < 0)
        {
            return ret;
        }

        ret = pouch_coap_check_response_code(&resp, path);
        if (ret < 0)
        {
            return ret;
        }

        /* Advance Block1 upload state */
        if (need_block1)
        {
            block_size_bytes =
                pouch_coap_negotiate_block1_size(&send_blk_ctx, &resp, block_size_bytes);
            send_blk_ctx.current += chunk_len;
        }

        upload_done = !need_block1 || send_blk_ctx.current >= payload_len;

        if (!upload_done && coap_header_get_code(&resp) != COAP_RESPONSE_CODE_CONTINUE)
        {
            LOG_ERR("Unexpected code 0x%02x during Block1 upload", coap_header_get_code(&resp));
            return -EPROTO;
        }

        /* Process Block2 response after upload completes */
        if (!upload_done || !resp_cb)
        {
            continue;
        }

        uint16_t resp_len = 0;
        const uint8_t *resp_data;
        int block2_opt;
        bool has_more = false;

        resp_data = coap_packet_get_payload(&resp, &resp_len);
        block2_opt = coap_get_option_int(&resp, COAP_OPTION_BLOCK2);

        if (block2_opt >= 0)
        {
            ret = pouch_coap_block_update(&resp, &recv_blk_ctx, &has_more);
            if (ret < 0)
            {
                return ret;
            }
        }

        if (resp_len > 0 || !has_more)
        {
            ret = resp_cb(resp_data, resp_len, true, !has_more, user_data);
            if (ret < 0)
            {
                return ret;
            }
        }

        if (has_more)
        {
            ret = pouch_coap_block2_fetch_remaining(path,
                                                    token,
                                                    COAP_TOKEN_MAX_LEN,
                                                    &recv_blk_ctx,
                                                    resp_cb,
                                                    user_data);
            if (ret < 0)
            {
                return ret;
            }
        }
    }

    return 0;
}
