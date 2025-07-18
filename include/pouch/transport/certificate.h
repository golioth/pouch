/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <pouch/types.h>
#include <zephyr/kernel.h>
#include <sys/types.h>

/** Set the server certificate. */
int pouch_server_certificate_set(const struct pouch_cert *cert);
/** Get the serial number of the current server certificate */
ssize_t pouch_server_certificate_serial_get(uint8_t *serial, size_t len);
/** Get the current device certificate reference */
int pouch_device_certificate_ref_get(uint8_t *cert_ref, size_t len);
/** Get the device's raw certificate. */
int pouch_device_certificate_get(struct pouch_cert *out);
