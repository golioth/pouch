/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @file
 * @brief Gateway cloud forwarder abstraction.
 *
 * The gateway library (src/gateway) routes BLE peripheral pouches
 * and device certificates to the cloud through whichever transport
 * the application registers at startup.  The transport implementation
 * lives in a port-specific module (e.g. port/zephyr/transport/coap)
 * and registers itself via @ref pouch_gateway_cloud_transport_register.
 *
 * The dispatch helpers (@ref pouch_gateway_cloud_forward_pouch and
 * friends) are called by the gateway library itself; applications do
 * not normally use them.
 */

/**
 * Callback invoked for each Block2 response chunk produced by a
 * cloud forwarder during a forward_pouch() call.
 *
 * @param data     Block payload data.
 * @param len      Length of the block payload.
 * @param is_last  True if this is the last block of the response.
 * @param arg      User-provided context pointer.
 *
 * @return 0 on success, negative errno on error.
 */
typedef int (*pouch_gateway_cloud_block2_cb_t)(const uint8_t *data,
                                               size_t len,
                                               bool is_last,
                                               void *arg);

/**
 * Cloud transport implementation registered with the gateway.
 *
 * Implementations are expected to be statically allocated by the
 * port and live for the lifetime of the application.
 */
struct pouch_gateway_cloud_transport
{
    /**
     * Make the cloud connection ready to forward pouches.
     *
     * For DTLS-based transports this typically connects the socket,
     * fetches the server certificate, uploads the gateway's own
     * device certificate, and forwards the server cert into the
     * gateway core via @ref pouch_gateway_server_cert_set.
     *
     * May be NULL if the transport has nothing to prepare.
     *
     * @return 0 on success, negative errno on error.
     */
    int (*ensure_ready)(void);

    /**
     * Forward a peripheral's pouch payload to the cloud.
     *
     * @param data      Pouch payload data.
     * @param len       Length of the payload.
     * @param resp_cb   Callback invoked for each Block2 response
     *                  block (downlink data). May be NULL if the
     *                  caller does not need the downlink.
     * @param arg       Context passed to @p resp_cb.
     *
     * @return 0 on success, negative errno on error.
     */
    int (*forward_pouch)(const uint8_t *data,
                         size_t len,
                         pouch_gateway_cloud_block2_cb_t resp_cb,
                         void *arg);

    /**
     * Upload a peripheral's device certificate to the cloud.
     *
     * @param cert  DER-encoded device certificate.
     * @param len   Length of the certificate.
     *
     * @return 0 on success, negative errno on error.
     */
    int (*upload_device_cert)(const uint8_t *cert, size_t len);
};

/**
 * Register a cloud transport implementation with the gateway.
 *
 * The transport descriptor must remain valid for the lifetime of
 * the application. Re-registering replaces the previous transport.
 *
 * @param transport  Implementation to register.
 */
void pouch_gateway_cloud_transport_register(const struct pouch_gateway_cloud_transport *transport);

/**
 * Get the currently registered cloud transport, or NULL.
 *
 * Intended primarily for tests that want to observe registration.
 */
const struct pouch_gateway_cloud_transport *pouch_gateway_cloud_transport_get(void);

/**
 * Dispatch helper: ensure the registered cloud transport is ready.
 *
 * @return 0 if no transport is registered or the transport reports
 *         success; negative errno otherwise.
 */
int pouch_gateway_cloud_ensure_ready(void);

/**
 * Dispatch helper: forward a pouch through the registered transport.
 *
 * @return -ENODEV if no transport is registered.
 */
int pouch_gateway_cloud_forward_pouch(const uint8_t *data,
                                      size_t len,
                                      pouch_gateway_cloud_block2_cb_t resp_cb,
                                      void *arg);

/**
 * Dispatch helper: upload a device certificate through the registered transport.
 *
 * @return -ENODEV if no transport is registered.
 */
int pouch_gateway_cloud_upload_device_cert(const uint8_t *cert, size_t len);
