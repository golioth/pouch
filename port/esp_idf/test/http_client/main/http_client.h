/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <pouch/pouch.h>
#include "mtls_type.h"

void http_client_transport_init(struct mtls_credentials *mtls_creds);
void http_client_fetch_manifest(struct mtls_credentials *mtls_creds);
void http_client_transport_sync();
