# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

# Physical adapters for the Pouch Serial transport. The serial protocol core
# lives in src/transport/serial and is compiled by src/transport/CMakeLists.txt;
# the files here bind that core to a concrete link (rpmsg/OpenAMP, and in future
# other buses).

if(CONFIG_POUCH_RPMSG_DEVICE)
    zephyr_library_sources(${CMAKE_CURRENT_LIST_DIR}/rpmsg_device.c)
endif()
