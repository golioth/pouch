/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stddef.h>
#include <zephyr/net/tls_credentials.h>


/**
 * Initialize the Pouch CoAP Client Transport
 *
 * @param sec_tag Sec tag where user added DTLS credentials
 *
 * @return 0 Success
 * @return non-zero Error code
 */
int pouch_coap_client_init(sec_tag_t sec_tag);

/**
 * Initiate the Pouch synchronization process
 *
 * Send uplink data to server and retrieve downlink data.
 *
 * @return 0 Success
 * @return non-zero Error code
 */
int pouch_coap_client_sync(void);
