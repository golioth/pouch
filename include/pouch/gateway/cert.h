/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

struct golioth_client;
struct pouch_gateway_device_cert_context;
struct pouch_gateway_server_cert_context;

#include <stdbool.h>
#include <stddef.h>

/* Max serial number length is 20 bytes according to spec:
 * https://datatracker.ietf.org/doc/html/rfc5280#section-4.1.2.2
 */
#define CERT_SERIAL_MAXLEN 20

/**
 * Start device certificate handling.
 *
 * @return Pointer to the device certificate context.
 */
struct pouch_gateway_device_cert_context *pouch_gateway_device_cert_start(void);

/**
 * Push data to the device certificate context.
 *
 * @param context The device certificate context.
 * @param data The data to push.
 * @param len The length of the data.
 * @return 0 on success, negative on error.
 */
int pouch_gateway_device_cert_push(struct pouch_gateway_device_cert_context *context,
                                   const void *data,
                                   size_t len);

/**
 * Abort device certificate handling.
 *
 * @param context The device certificate context.
 */
void pouch_gateway_device_cert_abort(struct pouch_gateway_device_cert_context *context);

/**
 * Finish device certificate handling.
 *
 * @param context The device certificate context.
 * @return 0 on success, negative on error.
 */
int pouch_gateway_device_cert_finish(struct pouch_gateway_device_cert_context *context);

/**
 * Start server certificate handling.
 *
 * @return Pointer to the server certificate context.
 */
struct pouch_gateway_server_cert_context *pouch_gateway_server_cert_start(void);

/**
 * Abort server certificate handling.
 *
 * @param context The server certificate context.
 */
void pouch_gateway_server_cert_abort(struct pouch_gateway_server_cert_context *context);

/**
 * Check if the server certificate is the newest.
 *
 * @param context The server certificate context.
 * @return True if newest, false otherwise.
 */
bool pouch_gateway_server_cert_is_newest(const struct pouch_gateway_server_cert_context *context);

/**
 * Check if the server certificate is complete.
 *
 * @param context The server certificate context.
 * @return True if complete, false otherwise.
 */
bool pouch_gateway_server_cert_is_complete(const struct pouch_gateway_server_cert_context *context);

/**
 * Get data from the server certificate context.
 *
 * @param context The server certificate context.
 * @param dst Destination buffer.
 * @param dst_len Length of the destination buffer.
 * @param is_last True if this is the last chunk.
 * @return 0 on success, negative on error.
 */
int pouch_gateway_server_cert_get_data(struct pouch_gateway_server_cert_context *context,
                                       void *dst,
                                       size_t *dst_len,
                                       bool *is_last);

/**
 * Get the serial number of the server certificate.
 *
 * @param dst Destination buffer.
 * @param[in,out] dst_len Length of the destination buffer. Set to the number of bytes written.
 */
void pouch_gateway_server_cert_get_serial(void *dst, size_t *dst_len);

/**
 * Callback when connected to Golioth client for certificate module.
 *
 * @param client The Golioth client.
 */
void pouch_gateway_cert_module_on_connected(struct golioth_client *client);
