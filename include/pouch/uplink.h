/*
 * Copyright (c) 2024 Golioth
 */

#include <stdint.h>

struct pouch_uplink_ctx;

struct pouch_uplink_ctx *pouch_uplink_begin(void);
ssize_t pouch_uplink_fill(struct pouch_uplink_ctx *ctx, void *buf, size_t buf_len);
int pouch_uplink_end(struct pouch_uplink_ctx *ctx);
