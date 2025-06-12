/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#pragma once

#include <zephyr/kernel.h>


/** Initial state of the buffer */
#define POUCH_BUF_STATE_INITIAL ((pouch_buf_state_t) 0)

/** Single pouch buffer */
struct pouch_buf;

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

size_t buf_read(const struct pouch_buf *buf, uint8_t *data, size_t len, size_t offset);

/** Get the number of buffers currently in flight */
int buf_active_count(void);

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
