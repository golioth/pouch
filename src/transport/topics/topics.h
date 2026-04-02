/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "../sar/receiver.h"
#include "../sar/sender.h"

extern struct pouch_sender pouch_topic_info;
extern struct pouch_sender pouch_topic_device_cert;
extern struct pouch_receiver pouch_topic_server_cert;
extern struct pouch_sender pouch_topic_uplink;
extern struct pouch_receiver pouch_topic_downlink;
