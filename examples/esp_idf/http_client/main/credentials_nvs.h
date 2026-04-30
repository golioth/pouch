/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <pouch/pouch.h>
#include <stdint.h>

typedef int (*cred_nvs_set_fn_t)(const char *buf);

int cred_set_wifi_ssid(const char *ssid);
int cred_set_wifi_psk(const char *psk);
int cred_set_device_crt(const char *b64_der);
int cred_set_device_key(const char *b64_der);

int cred_nvs_load_all(void);
const char *cred_get_wifi_ssid(void);
const char *cred_get_wifi_psk(void);
const uint8_t *cred_get_device_crt_der(size_t *len);
const uint8_t *cred_get_device_key_der(size_t *len);
