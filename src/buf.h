/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#pragma once

#include <zephyr/kernel.h>

#define POUCH_BUF_FROM_SNODE(n) CONTAINER_OF(n, struct pouch_buf, node)

/** Single pouch buffer */
struct pouch_buf
{
    sys_snode_t node;
    /** Number of bytes in the buffer */
    size_t bytes;
    /** Data */
    uint8_t buf[];
};

struct pouch_buf *buf_alloc(size_t size);

void buf_free(struct pouch_buf *buf);

void buf_write(struct pouch_buf *buf, const uint8_t *data, size_t len);

/** Get pointer to the next byte to write to */
uint8_t *buf_next(struct pouch_buf *buf);

size_t buf_read(struct pouch_buf *buf, uint8_t *data, size_t len, size_t offset);

/** Get the number of buffers currently in flight */
int buf_active_count(void);
