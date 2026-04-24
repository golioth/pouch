/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stddef.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/sys/clock.h>


/**
 * Initialize the Pouch HTTP Client Transport
 *
 * @param sec_tag Sec tag where user added TLS credentials
 * @param wait_for_conn Time to wait for a network connetion before failing
 *
 * @return 0 Success
 * @return non-zero Error code
 */
int pouch_http_client_init(sec_tag_t sec_tag, k_timeout_t wait_for_conn);

/**
 * Initiate the Pouch synchronization process
 *
 * Send uplink data to server that retrieve downlink data.
 *
 * @param wait_for_conn timeout to wait for a connection before failing
 *
 * @return 0 Success
 * @return non-zero Error code
 */
int pouch_http_client_sync(k_timeout_t wait_for_conn);
