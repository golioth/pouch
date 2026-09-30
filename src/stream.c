/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <pouch/port.h>
#include <pouch/uplink.h>
#include "buf.h"
#include "block.h"
#include "uplink.h"

/** Single stream instance */
struct pouch_stream
{
    /** Stream ID */
    uint8_t id;
    /** Buffer for stream data */
    struct pouch_buf *buf;
    /** Number of bytes written to the stream */
    size_t bytes;
    /** Session ID this stream was created for */
    uint32_t session_id;
};

/** Stream IDs currently held by open streams */
static POUCH_ATOMIC_DEFINE(stream_ids, BLOCK_ID_MASK + 1);

_Static_assert(POUCH_STREAMS_MAX <= BLOCK_ID_MASK, "Not enough stream IDs for POUCH_STREAMS_MAX");

static void write_stream_header(struct pouch_buf *block, uint16_t content_type, const char *path)
{
    size_t path_len = strlen(path);

    pouch_put_be16(content_type, buf_claim(block, sizeof(uint16_t)));
    *buf_claim(block, 1) = path_len;
    buf_write(block, (uint8_t *) path, path_len);
}

static uint8_t new_stream_id(void)
{
    // Pick the first unused stream ID:
    for (int id = 1; id < POUCH_STREAMS_MAX + 1; id++)
    {
        if (!pouch_atomic_test_and_set_bit(stream_ids, id))
        {
            return id;
        }
    }

    // No available stream IDs:
    return 0;
}

struct pouch_stream *pouch_uplink_stream_open(const char *path,
                                              uint16_t content_type,
                                              pouch_timeout_t timeout)
{
    if (strlen(path) > UINT8_MAX)
    {
        return NULL;
    }

    uint8_t stream_id = new_stream_id();
    if (!stream_id)
    {
        // Can't open more concurrent streams:
        return NULL;
    }

    struct pouch_stream *stream = malloc(sizeof(struct pouch_stream));
    if (stream == NULL)
    {
        pouch_atomic_clear_bit(stream_ids, stream_id);
        return NULL;
    }

    stream->id = stream_id;
    stream->bytes = 0;
    stream->session_id = uplink_session_id();

    stream->buf = block_alloc_stream(stream->id, true, timeout);
    if (stream->buf == NULL)
    {
        pouch_atomic_clear_bit(stream_ids, stream->id);
        free(stream);
        return NULL;
    }

    write_stream_header(stream->buf, content_type, path);

    return stream;
}

size_t pouch_stream_write(struct pouch_stream *stream,
                          const void *data,
                          size_t len,
                          pouch_timeout_t timeout)
{
    const uint8_t *bytes = data;
    size_t written = 0;

    if (!pouch_stream_is_valid(stream))
    {
        return 0;
    }

    while (written < len)
    {
        size_t space = block_space_get(stream->buf);
        if (space == 0)
        {
            /* We need a new buf, but we'll keep the old one around until we're sure a new one is
             * allocated, to make sure that the stream always has a buffer pointer. This lets us
             * mark the current block with "no more data" if the app decides to bail out on the
             * stream as a result of this failed write instead of having to allocate an empty block
             * for this.
             */
            struct pouch_buf *buf = block_alloc_stream(stream->id, false, timeout);
            if (buf == NULL)
            {
                break;
            }

            block_finish_stream(stream->buf, stream->id, false);
            uplink_enqueue(stream->buf);

            stream->buf = buf;
            space = block_space_get(stream->buf);
        }

        size_t write_len = MIN(space, len - written);
        buf_write(stream->buf, &bytes[written], write_len);
        written += write_len;
    }

    stream->bytes += written;

    return written;
}

int pouch_stream_close(struct pouch_stream *stream, pouch_timeout_t timeout)
{
    if (stream == NULL)
    {
        return -EINVAL;
    }

    if (pouch_stream_is_valid(stream) && stream->bytes > 0)
    {
        block_finish_stream(stream->buf, stream->id, true);
        uplink_enqueue(stream->buf);
    }
    else
    {
        block_free(stream->buf);
    }

    pouch_atomic_clear_bit(stream_ids, stream->id);
    free(stream);

    return 0;
}

bool pouch_stream_is_valid(struct pouch_stream *stream)
{
    return (stream != NULL) && (stream->session_id == uplink_session_id());
}

bool stream_is_open(void)
{
    return pouch_atomic_get_value(stream_ids) != 0;
}
