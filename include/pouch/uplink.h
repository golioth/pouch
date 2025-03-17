/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#pragma once

#include <pouch/types.h>

#include <zephyr/kernel.h>

/**
 * @file uplink.h
 * @brief Pouch uplink API for sending data to the cloud
 */

/** Maximum number of streams that can be open simultaneously in the same uplink session */
#define POUCH_STREAMS_MAX 126

struct pouch_stream;

/**
 * Write an entry to the pouch uplink.
 *
 * The content type is defined by the CoAP Content-Formats sub-registry within the IANA CoRE.
 * See @ref content_types.
 *
 * @param path The path to write the entry to.
 * @param content_type The content type of the entry.
 * @param data The data to write.
 * @param len The length of the data.
 * @param timeout The timeout for the operation.
 *
 * @return 0 on success or a negative error code on failure.
 */
int pouch_uplink_entry_write(const char *path,
                             uint16_t content_type,
                             const void *data,
                             size_t len,
                             k_timeout_t timeout);

/**
 * Close the current uplink session by finalizing the open pouch.
 *
 * @return 0 on success or a negative error code on failure.
 */
int pouch_uplink_close(k_timeout_t timeout);

/**
 * Open a new stream to the uplink.
 *
 * Note that the stream must be closed with @ref pouch_stream_close() before the uplink session is
 * closed with @ref pouch_uplink_close().
 *
 * The content type is defined by the CoAP Content-Formats sub-registry within the IANA CoRE.
 * See @ref content_types.
 *
 * @param path The path to write the entry to.
 * @param content_type The content type of the entry.
 *
 * @return A stream handle or NULL on error.
 */
struct pouch_stream *pouch_uplink_stream_open(const char *path, uint16_t content_type);

/**
 * Write data to a stream.
 *
 * The stream must be opened with @ref pouch_uplink_stream_open().
 *
 * The data is written to the stream in blocks. The number of bytes written may be less than the
 * requested length if pouch is unable to allocate enough memory to write the data. In this case,
 * the function will return the number of bytes written before the memory allocation failed.
 * The caller should retry the write operation with the remaining data once more memory is
 * available.
 *
 * @note This function is not thread-safe. The caller must ensure that only one thread writes to a
 * stream at a time.
 *
 * @param stream The stream to write to.
 * @param data The data to write.
 * @param len The length of the data.
 * @param timeout The timeout for the write operation. If the timeout is reached before the write
 * operation completes, the function will return the number of bytes written before the timeout.
 *
 * @return The number of bytes written.
 */
size_t pouch_stream_write(struct pouch_stream *stream,
                          const void *data,
                          size_t len,
                          k_timeout_t timeout);

/**
 * Close a stream.
 *
 * @param stream The stream to close.
 * @param timeout The timeout for the close operation.
 *
 * @return 0 on success or a negative error code on failure.
 */
int pouch_stream_close(struct pouch_stream *stream, k_timeout_t timeout);

/**
 * Check if a stream is valid.
 *
 * An invalid stream cannot be written to, and should be closed with @ref pouch_stream_close().
 * The data in the stream will not be processed in the cloud, but data usage may still be incurred
 * on stream data that has already been forwarded to the gateway.
 *
 * @param stream The stream to check.
 *
 * @return true if the stream is valid, false otherwise.
 */
bool pouch_stream_is_valid(struct pouch_stream *stream);
