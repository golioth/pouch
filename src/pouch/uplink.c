/*
 * Copyright (c) 2025 Golioth
 */

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "uplink.h"

static const char pouch_data[] =
    "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Nam eleifend placerat lorem, et interdum enim. Fusce iaculis commodo arcu vel consectetur. Cras id vulputate arcu, id tincidunt erat. Fusce faucibus efficitur sapien, non ultrices augue bibendum nam.";

struct pouch_uplink
{
    size_t offset;
    int error;
};

struct pouch_uplink *pouch_uplink_start(void)
{
    struct pouch_uplink *uplink = malloc(sizeof(struct pouch_uplink));
    if (NULL != uplink)
    {
        uplink->offset = 0;
    }

    return uplink;
}

enum pouch_result pouch_uplink_fill(struct pouch_uplink *uplink, void *dst, size_t *dst_len)
{
    enum pouch_result ret = POUCH_MORE_DATA;

    size_t bytes_in_payload = sizeof(pouch_data) - uplink->offset;
    size_t bytes_to_copy = *dst_len;

    if (bytes_to_copy >= bytes_in_payload)
    {
        bytes_to_copy = bytes_in_payload;
        ret = POUCH_NO_MORE_DATA;
    }

    memcpy(dst, pouch_data + uplink->offset, bytes_to_copy);

    *dst_len = bytes_to_copy;
    uplink->offset += bytes_to_copy;

    return ret;
}

int pouch_uplink_error(struct pouch_uplink *uplink)
{
    return uplink->error;
}

void pouch_uplink_finish(struct pouch_uplink *uplink)
{
    free(uplink);
}
