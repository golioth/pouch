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

/*
 * coap_bytes_to_block_size() in Zephyr silently rounds non
 * powers of two down to the nearest power of two, which would
 * make a misconfigured block size produce surprising on-the-wire
 * behavior. Reject non-powers-of-two at build time.
 */
BUILD_ASSERT((CONFIG_POUCH_COAP_BLOCK_SIZE & (CONFIG_POUCH_COAP_BLOCK_SIZE - 1)) == 0,
             "CONFIG_POUCH_COAP_BLOCK_SIZE must be a power of two");

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

/*
 * Validate that the response code matches one of the expected values.
 */
static int pouch_coap_require_code(const struct coap_packet *resp,
                                   const char *path,
                                   const uint8_t *expected,
                                   size_t expected_count)
{
    uint8_t code = coap_header_get_code(resp);

    for (size_t i = 0; i < expected_count; i++)
    {
        if (code == expected[i])
        {
            return 0;
        }
    }

    LOG_ERR("%s got unexpected code %d.%02d", path, code >> 5, code & 0x1f);
    return -EPROTO;
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

    ret = pouch_coap_check_response_code(resp, path);
    if (ret < 0)
    {
        return ret;
    }

    uint8_t expected_codes[] = {COAP_RESPONSE_CODE_CONTENT};
    return pouch_coap_require_code(resp, path, expected_codes, ARRAY_SIZE(expected_codes));
}

/*
 * Negotiate a potentially smaller block size from the server's
 * Block1 option in its response. Returns the (possibly updated)
 * block size in bytes.
 */
static uint16_t pouch_coap_negotiate_block1_size(struct coap_block_context *blk_ctx,
                                                 const struct coap_packet *resp)
{
    int block1_opt = coap_get_option_int(resp, COAP_OPTION_BLOCK1);

    if (block1_opt >= 0)
    {
        enum coap_block_size server_bs = block1_opt & 0x07;

        if (server_bs < blk_ctx->block_size)
        {
            blk_ctx->block_size = server_bs;
        }
    }

    return coap_block_size_to_bytes(blk_ctx->block_size);
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
                                             pouch_coap_block2_payload_cb_t resp_cb,
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
 * Append a Block1 option built from raw fields. Used by the streaming
 * variant where the total payload size is not known in advance, so
 * coap_append_block1_option() (which relies on block context state)
 * cannot be used directly.
 */
static int pouch_coap_append_block1(struct coap_packet *req,
                                    uint32_t num,
                                    bool more,
                                    enum coap_block_size szx)
{
    uint32_t block1 = (num << 4) | (more ? 0x08 : 0) | (szx & 0x07);

    return coap_append_option_int(req, COAP_OPTION_BLOCK1, block1);
}

/*
 * Process the Block2 response carried in the final Block1 response.
 *
 * Invokes @p resp_cb for the first block and, if more Block2 blocks
 * are pending, drives the remaining transfer.
 */
static int pouch_coap_process_block2_response(const char *path,
                                              const uint8_t *token,
                                              uint8_t token_len,
                                              const struct coap_packet *resp,
                                              struct coap_block_context *recv_blk_ctx,
                                              pouch_coap_block2_payload_cb_t resp_cb,
                                              void *user_data)
{
    uint16_t resp_len = 0;
    const uint8_t *resp_data;
    int block2_opt;
    bool has_more = false;
    int ret;

    resp_data = coap_packet_get_payload(resp, &resp_len);
    block2_opt = coap_get_option_int(resp, COAP_OPTION_BLOCK2);

    if (block2_opt >= 0)
    {
        ret = pouch_coap_block_update(resp, recv_blk_ctx, &has_more);
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
        return pouch_coap_block2_fetch_remaining(path,
                                                 token,
                                                 token_len,
                                                 recv_blk_ctx,
                                                 resp_cb,
                                                 user_data);
    }

    return 0;
}

/*
 * Streaming blockwise POST.
 *
 * Repeatedly calls @p chunk_cb to obtain payload chunks and uploads
 * each as a Block1 request. The total upload size does not need to
 * be known up front; the last chunk is identified by @p is_last set
 * by the callback.
 *
 * If @p resp_cb is non-NULL, the server's Block2 response is
 * delivered to it chunk-by-chunk.
 */
int pouch_coap_blockwise_post_streaming(const char *path,
                                        pouch_coap_upload_chunk_cb_t chunk_cb,
                                        void *chunk_user_data,
                                        pouch_coap_block2_payload_cb_t resp_cb,
                                        void *resp_user_data)
{
    static uint8_t chunk_buf[CONFIG_POUCH_COAP_BLOCK_SIZE];
    struct coap_block_context send_blk_ctx;
    struct coap_block_context recv_blk_ctx;
    struct coap_packet req;
    struct coap_packet resp;
    uint8_t token[COAP_TOKEN_MAX_LEN];
    uint16_t block_size_bytes;
    size_t total_sent = 0;
    size_t chunk_len = 0;
    bool is_last = false;
    bool need_block1;
    int ret;

    memcpy(token, coap_next_token(), COAP_TOKEN_MAX_LEN);

    coap_block_transfer_init(&send_blk_ctx, pouch_coap_block_size(), 0);
    coap_block_transfer_init(&recv_blk_ctx, pouch_coap_block_size(), 0);
    block_size_bytes = coap_block_size_to_bytes(send_blk_ctx.block_size);

    /* Fetch the first chunk to determine whether Block1 is required. */
    ret = chunk_cb(chunk_buf, block_size_bytes, &chunk_len, &is_last, chunk_user_data);
    if (ret < 0)
    {
        return ret;
    }

    if (chunk_len > block_size_bytes)
    {
        LOG_ERR("Chunk callback returned %zu bytes, exceeds block size %u",
                chunk_len,
                block_size_bytes);
        return -EINVAL;
    }

    need_block1 = !is_last;

    while (true)
    {
        if (!is_last && chunk_len != block_size_bytes)
        {
            LOG_ERR("Non-final chunk has %zu bytes, expected exactly %u",
                    chunk_len,
                    block_size_bytes);
            return -EINVAL;
        }

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
            uint32_t num = total_sent / block_size_bytes;

            ret = pouch_coap_append_block1(&req, num, !is_last, send_blk_ctx.block_size);
            if (ret < 0)
            {
                LOG_ERR("Failed to append Block1: %d", ret);
                return ret;
            }
        }

        /*
         * Include the Block2 option on every Block1 request so the
         * server applies the preferred response block size, including
         * on the final request that triggers the response body.
         */
        if (resp_cb)
        {
            ret = coap_append_block2_option(&req, &recv_blk_ctx);
            if (ret < 0)
            {
                LOG_ERR("Failed to append Block2: %d", ret);
                return ret;
            }
        }

        ret = pouch_coap_append_payload_chunk(&req, chunk_buf, 0, chunk_len);
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

        /* On the first response, adopt the server's negotiated block size. */
        if (need_block1 && total_sent == 0)
        {
            block_size_bytes = pouch_coap_negotiate_block1_size(&send_blk_ctx, &resp);
        }

        total_sent += chunk_len;

        if (is_last)
        {
            /*
             * The final POST must not be answered with 2.31 Continue.
             * That code is only valid for intermediate Block1 chunks.
             */
            static const uint8_t terminal_codes[] = {
                COAP_RESPONSE_CODE_CHANGED,
                COAP_RESPONSE_CODE_CONTENT,
                COAP_RESPONSE_CODE_CREATED,
            };

            ret = pouch_coap_require_code(&resp, path, terminal_codes, ARRAY_SIZE(terminal_codes));
            if (ret < 0)
            {
                return ret;
            }

            break;
        }

        if (coap_header_get_code(&resp) != COAP_RESPONSE_CODE_CONTINUE)
        {
            LOG_ERR("Unexpected code 0x%02x during Block1 upload", coap_header_get_code(&resp));
            return -EPROTO;
        }

        ret = chunk_cb(chunk_buf, block_size_bytes, &chunk_len, &is_last, chunk_user_data);
        if (ret < 0)
        {
            return ret;
        }

        if (chunk_len > block_size_bytes)
        {
            LOG_ERR("Chunk callback returned %zu bytes, exceeds block size %u",
                    chunk_len,
                    block_size_bytes);
            return -EINVAL;
        }
    }

    if (!resp_cb)
    {
        return 0;
    }

    return pouch_coap_process_block2_response(path,
                                              token,
                                              COAP_TOKEN_MAX_LEN,
                                              &resp,
                                              &recv_blk_ctx,
                                              resp_cb,
                                              resp_user_data);
}

/*
 * State used by the buffer-based blockwise_post wrapper to feed
 * pre-assembled bytes through the streaming chunk callback.
 */
struct buffer_upload_state
{
    const uint8_t *payload;
    size_t total;
    size_t offset;
};

static int buffer_upload_cb(uint8_t *out,
                            size_t buf_size,
                            size_t *chunk_len,
                            bool *is_last,
                            void *user_data)
{
    struct buffer_upload_state *state = user_data;
    size_t remaining = state->total - state->offset;
    size_t to_copy = MIN(remaining, buf_size);

    if (to_copy > 0)
    {
        memcpy(out, state->payload + state->offset, to_copy);
    }

    *chunk_len = to_copy;
    state->offset += to_copy;
    *is_last = (state->offset >= state->total);
    return 0;
}

/*
 * Buffer-based wrapper around pouch_coap_blockwise_post_streaming.
 *
 * Iterates over a contiguous buffer through buffer_upload_cb().
 */
int pouch_coap_blockwise_post(const char *path,
                              const uint8_t *payload,
                              size_t payload_len,
                              pouch_coap_block2_payload_cb_t resp_cb,
                              void *user_data)
{
    struct buffer_upload_state state = {
        .payload = payload,
        .total = payload_len,
        .offset = 0,
    };

    return pouch_coap_blockwise_post_streaming(path, buffer_upload_cb, &state, resp_cb, user_data);
}
