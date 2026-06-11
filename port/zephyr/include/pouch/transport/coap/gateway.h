/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

/**
 * @file
 * @brief Pouch CoAP cloud transport for the gateway.
 *
 * This transport implements the @ref pouch_gateway_cloud_transport
 * interface defined in <pouch/gateway/cloud.h>.  It re-uses the DTLS
 * connection set up by @ref pouch_coap_client_init to forward
 * peripheral pouches and device certificates to the cloud.
 */

/**
 * Register the Pouch CoAP transport as the gateway's cloud forwarder.
 *
 * Call this after @ref pouch_coap_client_init has stored the DTLS
 * sec_tag.  Once registered, the gateway library routes BLE
 * peripheral pouches and device certificates through the CoAP
 * transport.
 */
void pouch_coap_gateway_init(void);
