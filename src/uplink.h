/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#pragma once

#include "buf.h"

/** Initialize the pouch uplink handler */
int uplink_init(void);

void uplink_enqueue(struct pouch_buf *block);
