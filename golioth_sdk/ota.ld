/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/linker/iterable_sections.h>

ITERABLE_SECTION_ROM(golioth_ota_registered_component, 4)
ITERABLE_SECTION_ROM(golioth_ota_manifest_handler, 4)
