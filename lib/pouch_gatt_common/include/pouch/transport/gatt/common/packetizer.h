/*
 * Copyright (c) 2024 Golioth
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

struct pouch_gatt_packetizer;

enum pouch_gatt_packetizer_result
{
    POUCH_GATT_PACKETIZER_MORE_DATA,
    POUCH_GATT_PACKETIZER_NO_MORE_DATA,
    POUCH_GATT_PACKETIZER_EMPTY_PAYLOAD,
    POUCH_GATT_PACKETIZER_ERROR,
};

typedef enum pouch_gatt_packetizer_result (*pouch_gatt_packetizer_fill_cb)(void *dst,
                                                                           size_t *dst_len,
                                                                           void *user_arg);

struct pouch_gatt_packetizer *pouch_gatt_packetizer_start_buffer(const void *src, size_t src_len);
struct pouch_gatt_packetizer *pouch_gatt_packetizer_start_callback(pouch_gatt_packetizer_fill_cb cb,
                                                                   void *user_arg);
enum pouch_gatt_packetizer_result pouch_gatt_packetizer_get(
    struct pouch_gatt_packetizer *packetizer,
    void *dst,
    size_t *dst_len);
int pouch_gatt_packetizer_error(struct pouch_gatt_packetizer *packetizer);
void pouch_gatt_packetizer_finish(struct pouch_gatt_packetizer *packetizer);

ssize_t pouch_gatt_packetizer_decode(const void *buf,
                                     size_t buf_len,
                                     const void **payload,
                                     bool *is_first,
                                     bool *is_last,
                                     unsigned int *seq);
int pouch_gatt_packetizer_get_sequence(const void *packet, size_t length);
ssize_t pouch_gatt_ack_encode(void *buf, size_t buf_len, int seq, int window);
int pouch_gatt_ack_decode(const void *buf, size_t buf_len, unsigned int *seq, unsigned int *window);
