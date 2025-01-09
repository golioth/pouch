/*
 * Copyright (c) 2024 Golioth
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/uuid.h>

#define TF_UUID_GOLIOTH_SVC_VAL \
    BT_UUID_128_ENCODE(0x89a316ae, 0x89b7, 0x4ef6, 0xb1d3, 0x5c9a6e27d272)

#define TOOTHFAIRY_ADV_DATA BT_DATA_BYTES(BT_DATA_SVC_DATA128, TF_UUID_GOLIOTH_SVC_VAL, 0xA5)

struct toothfairy_peripheral;

struct toothfairy_peripheral *toothfairy_peripheral_create(void);
int toothfairy_peripheral_destroy(struct toothfairy_peripheral *tf_peripheral);
