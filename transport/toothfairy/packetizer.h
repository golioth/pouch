/*
 * Copyright (c) 2024 Golioth
 */

#include <stdbool.h>
#include <stddef.h>

struct tf_packetizer;

enum tf_packetizer_result
{
    TF_PACKETIZER_MORE_DATA,
    TF_PACKETIZER_NO_MORE_DATA,
    TF_PACKETIZER_ERROR,
};

typedef enum tf_packetizer_result (*tf_packetizer_fill_cb)(void *dst,
                                                           size_t *dst_len,
                                                           void *user_arg);

struct tf_packetizer *tf_packetizer_start_buffer(const void *src, size_t src_len);
struct tf_packetizer *tf_packetizer_start_callback(tf_packetizer_fill_cb cb, void *user_arg);
enum tf_packetizer_result tf_packetizer_get(struct tf_packetizer *packetizer,
                                            void *dst,
                                            size_t *dst_len);
int tf_packetizer_error(struct tf_packetizer *packetizer);
void tf_packetizer_finish(struct tf_packetizer *packetizer);
