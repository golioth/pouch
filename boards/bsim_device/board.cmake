# Copyright (c) 2025 Golioth, Inc.
# SPDX-License-Identifier: Apache-2.0

set(SUPPORTED_EMU_PLATFORMS native)

board_set_flasher_ifnset(bsim_device)
board_set_debugger_ifnset(bsim_device)
board_finalize_runner_args(bsim_device)

# Get BabbleSim binary name based on last segment in board qualifiers.
string(REGEX MATCH "([^/]*)$" BSIM_COMPONENT_SUFFIX "${BOARD_QUALIFIERS}")
set_property(TARGET runners_yaml_props_target PROPERTY exe_file ${ZEPHYR_BASE}/../tools/bsim/bin/bs_device_${BSIM_COMPONENT_SUFFIX})
