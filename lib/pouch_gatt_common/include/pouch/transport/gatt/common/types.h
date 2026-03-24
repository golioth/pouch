/*
 * Copyright (c) 2025 Golioth
 */

#include <zephyr/toolchain.h>
#include <stdint.h>
#include <stddef.h>
#include "uuids.h"

#define POUCH_GATT_VERSION 1

#define POUCH_GATT_ADV_VERSION_POUCH_SHIFT 4
#define POUCH_GATT_ADV_VERSION_POUCH_MASK 0xF0
#define POUCH_GATT_ADV_VERSION_SELF_SHIFT 0
#define POUCH_GATT_ADV_VERSION_SELF_MASK 0x0F

#define POUCH_GATT_ADV_FLAG_SYNC_REQUEST (1 << 0)


/**
 * Advertisement data payload.
 */
struct pouch_gatt_adv_data
{
    uint8_t version;
    uint8_t flags;
} __packed;

/**
 * AD data for Bluetooth advertisements.
 */
struct pouch_gatt_adv
{
    uint16_t uuid;
    struct pouch_gatt_adv_data payload;
} __packed;

/**
 * Initialize Pouch advertising data.
 */
#define POUCH_GATT_ADV_DATA_INIT                                             \
    {                                                                        \
        .uuid = POUCH_GATT_UUID_SVC_VAL_16, .payload = {                     \
            .version = (POUCH_VERSION << POUCH_GATT_ADV_VERSION_POUCH_SHIFT) \
                | (POUCH_GATT_VERSION << POUCH_GATT_ADV_VERSION_SELF_SHIFT), \
            .flags = 0x0,                                                    \
        }                                                                    \
    }

/**
 * Enable or disable the sync request flag for the advertising data.
 */
static inline void pouch_gatt_adv_req_sync(struct pouch_gatt_adv *data, bool req)
{
    if (req)
    {
        data->payload.flags |= POUCH_GATT_ADV_FLAG_SYNC_REQUEST;
    }
    else
    {
        data->payload.flags &= ~POUCH_GATT_ADV_FLAG_SYNC_REQUEST;
    }
}
