/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/sys/iterable_sections.h>

/**
 * @file downlink.h
 * @brief Pouch downlink API for receiving data from the cloud
 */

/**
 * Callback executed on start of downlink pouch entry.
 *
 * The content type is defined by the CoAP Content-Formats sub-registry within the IANA CoRE.
 * See @ref content_types.
 *
 * @param stream_id Stream ID (0 if not a stream).
 * @param path The path of the entry.
 * @param content_type The content type of the entry.
 */
typedef void (*pouch_downlink_start_cb)(unsigned int stream_id,
                                        const char *path,
                                        uint16_t content_type);

/**
 * Callback executed with reassembled downlink pouch entry.
 *
 * @param stream_id Stream ID (0 if not a stream).
 * @param data The data (payload) of the entry.
 * @param len The length of the data.
 * @param is_last Defines whether this is the last data fragment (last block) for given stream.
 */
typedef void (*pouch_downlink_data_cb)(unsigned int stream_id,
                                       const void *data,
                                       size_t len,
                                       bool is_last);

/**
 * Pouch event handler
 *
 * This structure is used to register a callback for a specific event.
 * Use the @ref POUCH_DOWNLINK_HANDLER macro to register an event handler.
 */
struct pouch_downlink_handler
{
    pouch_downlink_start_cb start_cb;
    pouch_downlink_data_cb data_cb;
};

/**
 * Register a downlink handler
 *
 * @param _start_cb The callback function to be called when the downlink pouch entry/stream starts
 * @param _data_cb The callback function to be called when the downlink pouch entry/stream is
 * reassembled
 */
#define POUCH_DOWNLINK_HANDLER(_start_cb, _data_cb)                                       \
    static const STRUCT_SECTION_ITERABLE(pouch_downlink_handler,                          \
                                         CONCAT(_pouch_downlink_handler_, _callback)) = { \
        .start_cb = _start_cb,                                                            \
        .data_cb = _data_cb};
