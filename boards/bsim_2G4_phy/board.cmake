# Copyright (c) 2025 Golioth, Inc.
# SPDX-License-Identifier: Apache-2.0

set(SUPPORTED_EMU_PLATFORMS native)

board_set_flasher_ifnset(bsim_phy)
board_set_debugger_ifnset(bsim_phy)
board_finalize_runner_args(bsim_phy)

set_property(TARGET runners_yaml_props_target PROPERTY exe_file ${ZEPHYR_BASE}/../tools/bsim/bin/bs_2G4_phy_v1)
