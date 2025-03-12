/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#pragma once

#include "buf.h"
#include <pouch/types.h>

/** Initialize the pouch uplink handler */
int uplink_init(const struct pouch_config *config);

void uplink_enqueue(struct pouch_buf *block);
