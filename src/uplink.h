/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#pragma once

#include "buf.h"

#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>

/** Initialize the pouch uplink handler */
int uplink_init(void);

void uplink_enqueue(struct pouch_buf *block);
