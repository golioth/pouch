/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#pragma once

#include <zephyr/sys_clock.h>

void pouch_downlink_entries_push(const uint8_t *buf, size_t buf_len, bool is_stream, bool is_last);
int entry_block_close(k_timeout_t timeout);
