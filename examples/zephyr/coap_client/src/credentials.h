/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <psa/crypto.h>
#include <pouch/types.h>
#include <zephyr/net/tls_credentials.h>

/**
 * Import the device private key into PSA
 *
 * @returns The assigned PSA key ID for the device's private key, or @c PSA_KEY_ID_NULL if the
 * private key could not be loaded.
 */
psa_key_id_t load_private_key(void);

/**
 * Load the device certificate.
 */
int load_certificate(struct pouch_cert *cert);

/** Free the device certificate. */
void free_certificate(struct pouch_cert *cert);

/**
 * Load server CA certificate needed for DTLS connection.
 */
int load_coap_server_ca(sec_tag_t sec_tag);

/**
 * Load Gateway device certificate needed for DTLS mTLS connection.
 */
int load_coap_gw_device_crt(sec_tag_t sec_tag);

/**
 * Load Gateway device private key needed for DTLS mTLS connection.
 */
int load_coap_gw_device_key(sec_tag_t sec_tag);

/**
 * Free DTLS credentials loaded from the filesystem.
 */
void free_coap_certs(void);
