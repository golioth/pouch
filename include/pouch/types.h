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

/**
 * The type of content in a pouch entry.
 *
 * The content type is used by Golioth to determine how to interpret the data in a pouch entry.
 */
enum pouch_content_type
{
    /** The content is a JSON encoded object */
    POUCH_CONTENT_TYPE_JSON = 50,
    /** The content is a CBOR encoded object */
    POUCH_CONTENT_TYPE_CBOR = 60,
    /** The content is a raw octet stream */
    POUCH_CONTENT_TYPE_OCTET_STREAM = 42,
};
