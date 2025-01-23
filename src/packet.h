/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#pragma once

#include <pouch/types.h>
#include <sys/types.h>

/**
 * Length of entry header in addition to length of path.
 *
 * The header consists of:
 * - 1 byte for the array tag, including a length of 3
 * - 2 bytes for the string tag plus a max length of 256
 * - 2 bytes for the content type
 * - 5 bytes for the data length
 */
#define POUCH_ENTRY_HEADER_OVERHEAD 10
#define POUCH_ENTRY_PATH_MAX_LEN 256

ssize_t entry_header_write(uint8_t *buf,
                           size_t maxlen,
                           const char *path,
                           enum pouch_content_type content_type,
                           size_t data_len);

/**
 * Encode the pouch header to the buffer.
 */
ssize_t pouch_header_write(uint8_t *buf, size_t maxlen);
