/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <pouch/pouch.h>
#include "http_client.h"

int fill_pouch_config(struct pouch_config *config);
void fill_mtls_credentials(struct mtls_credentials *creds);
