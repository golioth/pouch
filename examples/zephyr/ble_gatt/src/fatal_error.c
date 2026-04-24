/*
 * Copyright (c) 2025 Golioth
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log_ctrl.h>
#include <zephyr/fatal.h>
#include <zephyr/sys/reboot.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(fatal_error);

extern void sys_arch_reboot(int type);

void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
    LOG_PANIC();
    LOG_ERR("Resetting system");

    sys_reboot(SYS_REBOOT_COLD);
}
