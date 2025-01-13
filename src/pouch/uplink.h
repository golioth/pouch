/*
 * Copyright (c) 2025 Golioth
 */

enum pouch_result
{
    POUCH_MORE_DATA,
    POUCH_NO_MORE_DATA,
    POUCH_ERROR,
};

struct pouch_uplink;

struct pouch_uplink *pouch_uplink_start(void);
enum pouch_result pouch_uplink_fill(struct pouch_uplink *uplink, void *dst, size_t *dst_len);
int pouch_uplink_error(struct pouch_uplink *uplink);
void pouch_uplink_finish(struct pouch_uplink *uplink);
