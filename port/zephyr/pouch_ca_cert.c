/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <pouch/pouch_ca_cert.h>
#include <pouch/types.h>

static const uint8_t raw_ca_cert_der[] = {
#include "pouch_ca_cert.inc"
};

struct pouch_cert raw_ca_cert = {
    .buffer = raw_ca_cert_der,
    .size = sizeof(raw_ca_cert_der),
};

struct pouch_cert *pouch_ca_cert = &raw_ca_cert;
