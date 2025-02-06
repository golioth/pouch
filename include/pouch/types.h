/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * @file types.h
 * @brief Pouch type definitions
 */

/** The content is a raw octet stream */
#define POUCH_CONTENT_TYPE_OCTET_STREAM 42
/** The content is a JSON encoded object */
#define POUCH_CONTENT_TYPE_JSON 50
/** The content is a CBOR encoded object */
#define POUCH_CONTENT_TYPE_CBOR 60
