# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

if(NOT "${CONFIG_POUCH_TRANSPORT_BLE_GATT}" STREQUAL "y")
    return()
endif()

add_library(pouch_transport_gatt)

target_sources(pouch_transport_gatt PRIVATE
    ${POUCH_ROOT}/src/transport/endpoints/device/info.c
    ${POUCH_ROOT}/src/transport/endpoints/device/uplink.c
    ${POUCH_ROOT}/src/transport/endpoints/device/downlink.c
    ${POUCH_ROOT}/src/transport/sar/sender.c
    ${POUCH_ROOT}/src/transport/sar/receiver.c
    ${POUCH_ROOT}/src/transport/sar/packet.c
    peripheral.c
    ${CMAKE_CURRENT_BINARY_DIR}/info_encode.c
)

if("${CONFIG_POUCH_ENCRYPTION_SAEAD}" STREQUAL "y")
    target_sources(pouch_transport_gatt PRIVATE
        ${POUCH_ROOT}/src/transport/endpoints/device/device_cert.c
        ${POUCH_ROOT}/src/transport/endpoints/device/server_cert.c
    )
endif()

target_include_directories(pouch_transport_gatt PRIVATE
    ${POUCH_ROOT}/include
    ${POUCH_ROOT}/port/include
    ${POUCH_ROOT}/src
    ${POUCH_ROOT}/src/transport
    ${POUCH_ROOT}/port/esp_idf
    ${POUCH_ROOT}/port/esp_idf/include
    ${POUCH_ROOT}/port/esp_idf/lib/zcbor/zcbor_submodule/include
    ${CMAKE_CURRENT_BINARY_DIR}/include
)

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/info_encode.c
        COMMAND zcbor code -c ${POUCH_ROOT}/src/transport/info.cddl -t pouch_gatt_info -e
        --include-prefix cddl/ --output-c ${CMAKE_CURRENT_BINARY_DIR}/info_encode.c --output-h
        ${CMAKE_CURRENT_BINARY_DIR}/include/cddl/info_encode.h
        BYPRODUCTS include/cddl/info_encode.h include/cddl/info_encode_types.h
        DEPENDS ${POUCH_ROOT}/src/transport/info.cddl)
endif()

target_link_libraries(pouch_transport_gatt PUBLIC
    idf::bt
)
