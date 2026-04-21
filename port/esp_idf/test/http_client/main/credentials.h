/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <pouch/pouch.h>
#include "mtls_type.h"

int get_device_cert(struct pouch_cert *cert);
int fill_pouch_config(struct pouch_config *config);
void fill_mtls_credentials(struct mtls_credentials *creds);
