/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * MCUmgr Shell Server
 *
 * The MCUmgr subsystem auto-registers at SYS_INIT time:
 * - SMP UART transport (via CONFIG_MCUMGR_TRANSPORT_UART)
 * - Shell management group (via CONFIG_MCUMGR_GRP_SHELL)
 *
 * Zephyr's built-in shell commands (kernel version, kernel uptime, etc.)
 * are available automatically.  All this main() needs to do is keep the
 * system alive.
 */

#include <zephyr/kernel.h>

int main(void)
{
    printk("MCUmgr shell server ready\n");
    k_sleep(K_FOREVER);
    return 0;
}
