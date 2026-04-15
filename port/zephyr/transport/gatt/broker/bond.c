/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <pouch/gateway/bt/bond.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(bond, CONFIG_POUCH_GATEWAY_GATT_LOG_LEVEL);

static atomic_t pouch_gateway_bonding;

static void pouch_gateway_bonding_timeout_handle(struct k_work *work)
{
    if (atomic_set(&pouch_gateway_bonding, 0))
    {
        LOG_INF("Bonding disabled on timeout");
    }
}

K_WORK_DELAYABLE_DEFINE(pouch_gateway_bonding_timeout_work, pouch_gateway_bonding_timeout_handle);

void pouch_gateway_bonding_enable(k_timeout_t timeout)
{
    if (!atomic_set(&pouch_gateway_bonding, 1))
    {
        LOG_INF("Bonding enabled");
    }
    k_work_reschedule(&pouch_gateway_bonding_timeout_work, timeout);
}

void pouch_gateway_bonding_disable(void)
{
    k_work_cancel_delayable(&pouch_gateway_bonding_timeout_work);
    if (atomic_set(&pouch_gateway_bonding, 0))
    {
        LOG_INF("Bonding disabled");
    }
}

bool pouch_gateway_bonding_is_enabled(void)
{
    return atomic_get(&pouch_gateway_bonding);
}
