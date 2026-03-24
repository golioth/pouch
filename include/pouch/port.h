/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "pouch_port_macros_internal.h"

/*--------------------------------------------------
 * Application Startup Hook
 *------------------------------------------------*/

/** Register function to run at application startup
 *
 * Example:
 *
 * static void my_startup_function(void)
 * {
 *     initialize_something();
 * }
 * POUCH_APPLICATION_STARTUP_HOOK(my_startup_function);
 *
 */
#define POUCH_APPLICATION_STARTUP_HOOK(_function) POUCH_APPLICATION_STARTUP_HOOK_INTERNAL(_function)
