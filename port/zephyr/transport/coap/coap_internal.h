/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Private header shared between client.c and blockwise.c.
 * Not part of the public API — do not include outside this
 * transport directory.
 */

#pragma once

#include <zephyr/net/coap.h>

/* Callback type for processing Block2 response payload chunks. */
typedef int (*pouch_coap_block2_payload_cb_t)(const uint8_t *data,
                                              size_t len,
                                              bool first_block,
                                              bool last_block,
                                              void *user_data);

/*
 * Callback type for supplying upload payload chunks to a streaming
 * blockwise POST.
 *
 * The callback fills @p buf with up to @p buf_size bytes and reports
 * the actual count in @p chunk_len. @p is_last must be set to true
 * when no further chunks will be produced. The callback should fill
 * exactly @p buf_size bytes for every non-final chunk so that all
 * intermediate Block1 chunks match the negotiated block size.
 */
typedef int (*pouch_coap_upload_chunk_cb_t)(uint8_t *buf,
                                            size_t buf_size,
                                            size_t *chunk_len,
                                            bool *is_last,
                                            void *user_data);

/*--------------------------------------------------
 * Functions provided by client.c
 *------------------------------------------------*/

int pouch_coap_build_request(struct coap_packet *pkt,
                             uint8_t method,
                             const uint8_t *token,
                             uint8_t token_len,
                             uint16_t msg_id,
                             const char *path,
                             int content_format);

int pouch_coap_send_and_recv(struct coap_packet *req,
                             struct coap_packet *resp,
                             const uint8_t *token,
                             uint8_t token_len);

/*--------------------------------------------------
 * Functions provided by blockwise.c
 *------------------------------------------------*/

int pouch_coap_blockwise_get(const char *path, uint8_t *buf, size_t buf_size, size_t *out_len);

/*
 * Streaming blockwise POST.
 *
 * Repeatedly invokes @p chunk_cb to obtain upload payload chunks and
 * sends them via Block1. When the response carries Block2 data,
 * @p resp_cb is invoked for each received chunk.
 */
int pouch_coap_blockwise_post_streaming(const char *path,
                                        pouch_coap_upload_chunk_cb_t chunk_cb,
                                        void *chunk_user_data,
                                        pouch_coap_block2_payload_cb_t resp_cb,
                                        void *resp_user_data);

/*
 * Buffer-based wrapper around pouch_coap_blockwise_post_streaming.
 * Convenient for small, already-assembled payloads (e.g. certificates).
 */
int pouch_coap_blockwise_post(const char *path,
                              const uint8_t *payload,
                              size_t payload_len,
                              pouch_coap_block2_payload_cb_t resp_cb,
                              void *user_data);
