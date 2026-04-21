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
typedef int (*block2_payload_cb_t)(const uint8_t *data,
                                   size_t len,
                                   bool first_block,
                                   bool last_block,
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

int pouch_coap_blockwise_post(const char *path,
                              const uint8_t *payload,
                              size_t payload_len,
                              block2_payload_cb_t resp_cb,
                              void *user_data);
