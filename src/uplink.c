/*
 * Copyright (c) 2024 Golioth
 */

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

struct pouch_uplink_ctx
{
    void *unused;
};

struct pouch_uplink_ctx *pouch_uplink_begin(void)
{
    struct pouch_uplink_ctx *ctx = malloc(sizeof(struct pouch_uplink_ctx));

    return ctx;
}

int pouch_uplink_fill(struct pouch_uplink_ctx *ctx, void *buf, size_t buf_len)
{
    if (NULL == ctx)
    {
        return -EINVAL;
    }

    return 0;
}

int pouch_uplink_end(struct pouch_uplink_ctx *ctx)
{
    free(ctx);

    return 0;
}
