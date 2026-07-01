# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

pouch_config_enabled(_pouch_transport_ble_gatt CONFIG_POUCH_TRANSPORT_BLE_GATT)
if(NOT _pouch_transport_ble_gatt)
    return()
endif()

target_sources(${POUCH_ACTIVE_TARGET} PRIVATE peripheral.c)

target_include_directories(${POUCH_ACTIVE_TARGET} PRIVATE
    # Access to shared headers like: #include "transport/sar/receiver.h"
    ${POUCH_ROOT}/src
)
