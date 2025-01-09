/*
 * Copyright (c) 2024 Golioth
 */

#include <stdlib.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

#include "toothfairy_declarations.h"
#include <toothfairy/peripheral.h>

#include "toothfairy_uuids.h"

struct toothfairy_peripheral
{
    void *unused;
};

static const struct bt_uuid_128 tf_svc_uuid = BT_UUID_INIT_128(TF_UUID_GOLIOTH_SVC_VAL);

TOOTHFAIRY_SERVICE(&tf_svc_uuid);

static struct bt_gatt_service golioth_svc = {
    .attrs = TOOTHFAIRY_ATTR_ARRAY_PTR,
};

struct toothfairy_peripheral *toothfairy_peripheral_create(void)
{
    struct toothfairy_peripheral *tf = malloc(sizeof(struct toothfairy_peripheral));
    if (NULL == tf)
    {
        goto finish;
    }

    TOOTHFAIRY_ATTR_ARRAY_LEN(&golioth_svc.attr_count);
    int err = bt_gatt_service_register(&golioth_svc);
    if (0 != err)
    {
        free(tf);
        tf = NULL;
    }

    STRUCT_SECTION_FOREACH(toothfairy_characteristic, tf_chrc)
    {
        if (tf_chrc->init)
        {
            tf_chrc->init(tf, tf_chrc->attr);
        }
    }

finish:
    return tf;
}

int toothfairy_peripheral_destroy(struct toothfairy_peripheral *tf_peripheral)
{
    free(tf_peripheral);

    return 0;
}
