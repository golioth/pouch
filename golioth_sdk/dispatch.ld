/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/linker/iterable_sections.h>

ITERABLE_SECTION_ROM(golioth_downlink_service, 4)

ITERABLE_SECTION_ROM(golioth_uplink_service, 4)
