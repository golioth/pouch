/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <pouch/pouch_ca_cert.h>
#include <pouch/types.h>
#include <pouch/port.h>

extern const uint8_t pouch_ca_cert_der_start[] asm("_binary_pouch_ca_cert_der_start");
extern const uint8_t pouch_ca_cert_der_end[] asm("_binary_pouch_ca_cert_der_end");

struct pouch_cert raw_ca_cert;

static void populate_ca_cert_struct(void)
{
    raw_ca_cert.buffer = pouch_ca_cert_der_start;
    raw_ca_cert.size = pouch_ca_cert_der_end - pouch_ca_cert_der_start;
};
POUCH_APPLICATION_STARTUP_HOOK(populate_ca_cert_struct);

struct pouch_cert *pouch_ca_cert = &raw_ca_cert;
