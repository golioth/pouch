# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

if("${CONFIG_POUCH_TRANSPORT_HTTP_CLIENT}" STREQUAL "y")
    add_subdirectory(http)
endif()

if("${CONFIG_POUCH_TRANSPORT_BLE_GATT}" STREQUAL "y")
    add_subdirectory(gatt)
endif()
