/*
 * Copyright (c) 2024 Golioth
 */

#include <stdlib.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/uuid.h>

#include <toothfairy/peripheral.h>

#include "toothfairy_uuids.h"

struct toothfairy_peripheral
{
    void *unused;
};

struct toothfairy_peripheral *toothfairy_peripheral_create(void)
{
    return malloc(sizeof(struct toothfairy_peripheral));
}

int toothfairy_peripheral_destroy(struct toothfairy_peripheral *tf_peripheral)
{
    free(tf_peripheral);

    return 0;
}
