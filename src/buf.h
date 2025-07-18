/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <zephyr/kernel.h>


/** Initial state of the buffer */
#define POUCH_BUF_STATE_INITIAL ((pouch_buf_state_t) 0)

/** Single pouch buffer */
struct pouch_buf;

/** Buffer view for reading data out of a buffer. */
struct pouch_bufview
{
    const struct pouch_buf *buf;
    size_t offset;
};

/** Buffer state */
typedef size_t pouch_buf_state_t;

/** Buffer queue */
typedef sys_slist_t pouch_buf_queue_t;

struct pouch_buf *buf_alloc(size_t size);

void buf_free(struct pouch_buf *buf);

/**
 * Claim a number of bytes from the buffer.
 *
 * Returns a pointer to the claimed bytes.
 */
uint8_t *buf_claim(struct pouch_buf *buf, size_t bytes);

void buf_write(struct pouch_buf *buf, const uint8_t *data, size_t len);

/** Get a state of the buffer that we can restore it to later */
pouch_buf_state_t buf_state_get(const struct pouch_buf *buf);

/** Reset the buffer to a previous state */
void buf_restore(struct pouch_buf *buf, pouch_buf_state_t state);

/** Get the number of bytes in the buffer */
size_t buf_size_get(const struct pouch_buf *buf);

/** Get pointer to the next byte to write to */
uint8_t *buf_next(struct pouch_buf *buf);

/** Get the number of buffers currently in flight */
int buf_active_count(void);

/**
 * Trim start of buffer by @a bytes
 *
 * @return Number of bytes actually trimmed
 */
size_t buf_trim_start(struct pouch_buf *buf, size_t bytes);

/**
 * Trim end of buffer by @a bytes
 *
 * @return Number of bytes actually trimmed
 */
size_t buf_trim_end(struct pouch_buf *buf, size_t bytes);

/** Initialize a buffer queue */
void buf_queue_init(pouch_buf_queue_t *queue);

/** Submit a buffer to the queue */
void buf_queue_submit(pouch_buf_queue_t *queue, struct pouch_buf *buf);

/** Get a buffer from the queue */
struct pouch_buf *buf_queue_get(pouch_buf_queue_t *queue);

/** Peek at the next buffer in the queue */
struct pouch_buf *buf_queue_peek(pouch_buf_queue_t *queue);

/** Check if the queue is empty */
static inline bool buf_queue_is_empty(pouch_buf_queue_t *queue)
{
    return buf_queue_peek(queue) == NULL;
}

/** Initialize a buffer view */
static inline void pouch_bufview_init(struct pouch_bufview *v, const struct pouch_buf *buf)
{
    v->buf = buf;
    v->offset = 0;
}

/** Read data from the buffer view */
size_t pouch_bufview_memcpy(struct pouch_bufview *v, void *dst, size_t bytes);

/** Read available data, if the requested amount is available. */
const void *pouch_bufview_read(struct pouch_bufview *v, size_t bytes);

/** Read a byte from the buffer view */
uint8_t pouch_bufview_read_byte(struct pouch_bufview *v);

/** Read a big endian uint16_t from the buffer view */
uint16_t pouch_bufview_read_be16(struct pouch_bufview *v);

/** Read a big endian uint32_t from the buffer view */
uint32_t pouch_bufview_read_be32(struct pouch_bufview *v);

/** Read a big endian uint64_t from the buffer view */
uint64_t pouch_bufview_read_be64(struct pouch_bufview *v);

/** Get the number of bytes available for reading */
size_t pouch_bufview_available(const struct pouch_bufview *v);

/** Check whether the buffer view is initialized and has data */
static inline bool pouch_bufview_is_ready(const struct pouch_bufview *v)
{
    return v->buf != NULL && pouch_bufview_available(v) > 0;
}

/** Free the associated buffer and reset the bufview. */
static inline void pouch_bufview_free(struct pouch_bufview *v)
{
    buf_free((struct pouch_buf *) v->buf);
    v->buf = NULL;
    v->offset = 0;
}
