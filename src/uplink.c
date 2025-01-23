/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#include "packet.h"
#include "pouch.h"
#include "block.h"

#include <pouch/events.h>
#include <pouch/transport/uplink.h>
#include <pouch/types.h>

#include <zephyr/init.h>
#include <zephyr/sys/iterable_sections.h>
#include <zephyr/sys/ring_buffer.h>

enum flags
{
    POUCH_READY,
    SESSION_ACTIVE,
    FLUSHED,
    POUCH_CLOSED,
};

struct pouch_uplink
{
    atomic_t flags;
    int error;
    struct ring_buf ringbuf;
    uint8_t ringbuf_buffer[CONFIG_POUCH_BUF_SIZE];
    struct k_mutex mutex;
    struct k_sem sem;
};

/** Single uplink session */
static struct pouch_uplink uplink;

static bool pouch_is_ready(struct pouch_uplink *uplink)
{
    return atomic_get(&uplink->flags) & BIT(POUCH_READY);
}

static bool session_is_active(struct pouch_uplink *uplink)
{
    return atomic_get(&uplink->flags) & BIT(SESSION_ACTIVE);
}

static bool pouch_is_open(struct pouch_uplink *uplink)
{
    return !(atomic_get(&uplink->flags) & BIT(POUCH_CLOSED));
}

/** Clear ring buffer and write header */
static int initialize_ringbuf(struct pouch_uplink *uplink)
{
    ring_buf_reset(&uplink->ringbuf);

    uint8_t *buf;
    size_t max_size = ring_buf_put_claim(&uplink->ringbuf, &buf, CONFIG_POUCH_BUF_SIZE);

    ssize_t res = pouch_header_write(buf, max_size);
    if (res < 0)
    {
        ring_buf_put_finish(&uplink->ringbuf, 0);
        return res;
    }

    ring_buf_put_finish(&uplink->ringbuf, res);
    return 0;
}

int uplink_init(void)
{
    ring_buf_init(&uplink.ringbuf, sizeof(uplink.ringbuf_buffer), uplink.ringbuf_buffer);
    k_mutex_init(&uplink.mutex);
    k_sem_init(&uplink.sem, 0, 1);

    return 0;
}

int uplink_enqueue(const uint8_t *data, size_t len, k_timeout_t timeout)
{
    uint32_t bytes_written = 0;
    int err;

    /* Lock the buffer while we're pushing data. The semaphore on its own is not enough, as we need
     * to ensure that the buffer is not being written to by another thread between loops.
     */
    err = k_mutex_lock(&uplink.mutex, timeout);
    if (err)
    {
        return err;
    }

    // Wait until a new pouch can be created if the current one is closed
    while (!pouch_is_open(&uplink))
    {
        err = k_sem_take(&uplink.sem, timeout);
        if (err)
        {
            return err;
        }
    }

    if (!atomic_test_and_set_bit(&uplink.flags, POUCH_READY))
    {
        err = initialize_ringbuf(&uplink);
        if (err)
        {
            atomic_clear_bit(&uplink.flags, POUCH_READY);
            goto out;
        }
    }

    while (pouch_is_open(&uplink))
    {
        bytes_written += ring_buf_put(&uplink.ringbuf, &data[bytes_written], len - bytes_written);
        if (bytes_written == len)
        {
            break;
        }

        // wait for reader to make space
        err = k_sem_take(&uplink.sem, timeout);
        if (err)
        {
            goto out;
        }
    }

    if (bytes_written < len)
    {
        // Write was abandoned due to pouch being closed
        err = -EAGAIN;
    }

out:
    k_mutex_unlock(&uplink.mutex);

    return err;
}

size_t uplink_pending(void)
{
    return ring_buf_size_get(&uplink.ringbuf);
}

// Transport API:

struct pouch_uplink *pouch_uplink_start(void)
{
    if (atomic_test_and_set_bit(&uplink.flags, SESSION_ACTIVE))
    {
        return NULL;
    }

    pouch_event_emit(POUCH_EVENT_SESSION_START);

    return &uplink;
}

enum pouch_result pouch_uplink_fill(struct pouch_uplink *uplink, void *dst, size_t *dst_len)
{
    if (!session_is_active(uplink))
    {
        return POUCH_ERROR;
    }

    if (!pouch_is_ready(uplink))
    {
        *dst_len = 0;
        return POUCH_NO_MORE_DATA;
    }

    uint32_t available = ring_buf_size_get(&uplink->ringbuf);
    uint32_t bytes_read = ring_buf_get(&uplink->ringbuf, dst, *dst_len);

    // Signal to writers that there is space available
    k_sem_give(&uplink->sem);

    *dst_len = bytes_read;

    if (available > bytes_read)
    {
        return POUCH_MORE_DATA;
    }

    return POUCH_NO_MORE_DATA;
}

void pouch_uplink_flush(struct pouch_uplink *uplink)
{
    if (atomic_test_and_set_bit(&uplink->flags, FLUSHED))
    {
        return;
    }

    // ignoring any errors here
    block_flush(K_NO_WAIT);

    atomic_set_bit(&uplink->flags, POUCH_CLOSED);
}

int pouch_uplink_error(struct pouch_uplink *uplink)
{
    return uplink->error;
}

void pouch_uplink_finish(struct pouch_uplink *uplink)
{
    uplink->error = 0;
    atomic_clear(&uplink->flags);
    k_sem_give(&uplink->sem);
    pouch_event_emit(POUCH_EVENT_SESSION_END);
}
