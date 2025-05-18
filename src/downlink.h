/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#pragma once

#include <stdbool.h>
#include <pouch/types.h>

/** Initialize the pouch downlink handler */
int downlink_init(const struct pouch_config *config);
