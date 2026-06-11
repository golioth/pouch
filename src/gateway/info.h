/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct pouch_gateway_info_context;

/**
 * @brief Start info read operation.
 *
 * @return Pointer to the info context, or NULL on failure.
 */
struct pouch_gateway_info_context *pouch_gateway_info_start(void);
/**
 * @brief Push data to the info context.
 *
 * @param context The info context.
 * @param data The data to push.
 * @param len The length of the data.
 * @return 0 on success, negative on error.
 */
int pouch_gateway_info_push(struct pouch_gateway_info_context *context,
                            const void *data,
                            size_t len);
/**
 * @brief Abort the info read operation.
 *
 * @param context The info context.
 */
void pouch_gateway_info_abort(struct pouch_gateway_info_context *context);
/**
 * @brief Finish the info read operation.
 *
 * @param context The info context.
 * @param[out] server_cert_provisioned Set to true if server cert is provisioned.
 * @param[out] device_cert_provisioned Set to true if device cert is provisioned.
 * @return 0 on success, negative on error.
 */
int pouch_gateway_info_finish(struct pouch_gateway_info_context *context,
                              bool *server_cert_provisioned,
                              bool *device_cert_provisioned);
