/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#pragma once

#include <pouch/transport/uplink.h>
#include <stdint.h>

void transport_session_start(void);

void transport_session_end(void);

enum pouch_result transport_pull_data(uint8_t *dst, size_t *len);

void transport_flush(void);

void transport_reset(void *unused);
