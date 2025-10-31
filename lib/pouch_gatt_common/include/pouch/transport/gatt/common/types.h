/*
 * Copyright (c) 2025 Golioth
 */

#include <zephyr/toolchain.h>

#define POUCH_GATT_VERSION 1

#define POUCH_GATT_ADV_VERSION_POUCH_SHIFT 4
#define POUCH_GATT_ADV_VERSION_POUCH_MASK 0xF0
#define POUCH_GATT_ADV_VERSION_SELF_SHIFT 0
#define POUCH_GATT_ADV_VERSION_SELF_MASK 0x0F

#define POUCH_GATT_ADV_FLAG_SYNC_REQUEST (1 << 0)

struct pouch_gatt_adv_data
{
    uint8_t version;
    uint8_t flags;
} __packed;
