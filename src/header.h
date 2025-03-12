/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#pragma once

#include "buf.h"
#include <pouch/types.h>

/**
 * Allocate and encode a pouch header.
 */
struct pouch_buf *pouch_header_create(const struct pouch_config *config);
