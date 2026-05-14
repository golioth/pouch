/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <errno.h>
#include "bpickle.h"
#include "register.h"
#include "pouch/uplink.h"
#include "pouch/types.h"

static const char *ubuntu_pro =
    "{\"attached\":false,\"contract\":{\"id\":\"\",\"name\":\"\",\"products\":[\"landscape\"]},\"effective\":\"2026-05-14T11:33:43.496970+00:00\",\"expires\":\"2026-06-13T11:33:43.496970+00:00\",\"result\":\"success\"}";


static struct bpickle_val *make_register_msg(const struct landscape *ls,
                                             const struct landscape_device *dev)
{
    struct bpickle_val *msg = bpickle_dict_new();
    if (!msg)
    {
        return NULL;
    }

    if (bpickle_dict_set_str(msg, "type", bpickle_str("register")) < 0
        || bpickle_dict_set_str(msg, "account_name", bpickle_str(ls->account_name)) < 0
        || bpickle_dict_set_str(msg, "hostname", bpickle_str(dev->hostname)) < 0
        || bpickle_dict_set_str(msg, "access_group", bpickle_str("")) < 0
        || bpickle_dict_set_str(msg, "computer_title", bpickle_str(dev->computer_title)) < 0
        || bpickle_dict_set_str(msg, "ubuntu_pro_info", bpickle_str(ubuntu_pro)) < 0)
    {
        bpickle_val_free(msg);
        return NULL;
    }

    return msg;
}
/*
{'messages': [{'access_group': '',
               'account_name': 'gbpnwxuq',
               'computer_title': 'turtle',
               'hostname': 'hey-9215878805233253',
               'machine_id': '123456',
               'type': 'register',
               'ubuntu_pro_info':
'{"attached":false,"contract":{"id":"","name":"","products":["landscape"]},"effective":"2026-05-14T11:33:43.496970+00:00","expires":"2026-06-13T11:33:43.496970+00:00","result":"success"}'}]}
*/

static struct bpickle_val *make_register_info(const struct landscape *ls,
                                              const struct landscape_device *dev)
{
    struct bpickle_val *info = bpickle_dict_new();
    if (!info)
    {
        return NULL;
    }

    struct bpickle_val *message = make_register_msg(ls, dev);
    if (!message)
    {
        bpickle_val_free(info);
        return NULL;
    }

    struct bpickle_val *messages = bpickle_list_new();
    if (!messages)
    {
        bpickle_val_free(info);
        return NULL;
    }

    if (bpickle_list_append(messages, message) < 0
        || bpickle_dict_set_str(info, "messages", messages) < 0)
    {
        bpickle_val_free(info);
        bpickle_val_free(messages);
        return NULL;
    }

    return info;
}

int landscape_register(const struct landscape *ls, const struct landscape_device *device)
{
    struct bpickle_val *info = make_register_info(ls, device);
    if (!info)
    {
        return -ENOMEM;
    }

    struct bpickle_buf buf = {0};
    int err = bpickle_dumps(info, &buf);
    bpickle_val_free(info);
    if (err)
    {
        return -ENOMEM;
    }

    err = pouch_uplink_entry_write("register",
                                   POUCH_CONTENT_TYPE_OCTET_STREAM,
                                   buf.data,
                                   buf.len,
                                   POUCH_SECONDS(1));
    bpickle_buf_free(&buf);
    if (err)
    {
        return err;
    }

    return 0;
}
