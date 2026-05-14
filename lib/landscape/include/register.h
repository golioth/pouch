/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
// du8:messagesldu12:access_groupu0:u12:account_nameu8:gbpnwxuqu14:computer_titleu6:turtleu8:hostnameu20:hey-9215878805233253u10:machine_idu6:123456u4:typeu8:registeru15:ubuntu_pro_infou202:{"attached":false,"contract":{"id":"1234SNAP-CONTRACT","name":"","products":["landscape"]},"effective":"2026-05-14T11:33:43.496970+00:00","expires":"2026-06-13T11:33:43.496970+00:00","result":"success"};;;

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

struct landscape
{
    const char *account_name;
};

struct landscape_device
{
    const char *computer_title;
    const char *hostname;
};

int landscape_register(const struct landscape *ls, const struct landscape_device *device);
