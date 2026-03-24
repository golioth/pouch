# Copyright (c) 2025 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_link_libraries(pouch)

zephyr_library_sources(
    endpoints/info.c
    endpoints/uplink.c
    endpoints/downlink.c
    sar/sender.c
    sar/receiver.c
    sar/packet.c
    ${CMAKE_CURRENT_BINARY_DIR}/info_encode.c
)

zephyr_library_sources_ifdef(CONFIG_POUCH_ENCRYPTION_SAEAD
    endpoints/device_cert.c
    endpoints/server_cert.c
)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/info_encode.c
    COMMAND zcbor code -c ${CMAKE_CURRENT_LIST_DIR}/info.cddl -t
    pouch_gatt_info -e --include-prefix cddl/ --output-c ${CMAKE_CURRENT_BINARY_DIR}/info_encode.c --output-h ${CMAKE_CURRENT_BINARY_DIR}/include/cddl/info_encode.h
    BYPRODUCTS include/cddl/info_encode.h include/cddl/info_encode_types.h
    DEPENDS info.cddl)

zephyr_library_include_directories(${CMAKE_CURRENT_LIST_DIR})
zephyr_library_include_directories(${CMAKE_CURRENT_BINARY_DIR}/include)
