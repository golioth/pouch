# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

zephyr_library_named(pouch)
zephyr_include_directories(../include)      # pouch/port.h
zephyr_include_directories(include)         # Zephyr port includes
zephyr_library_sources(zephyr_port_layer.c) # Port functions

if(CONFIG_POUCH)

    zephyr_linker_sources(SECTIONS downlink_handlers.ld)
    zephyr_linker_sources(SECTIONS uplink_handlers.ld)
    zephyr_linker_sources(SECTIONS event_handlers.ld)

    zephyr_library_link_libraries(mbedTLS)

    if (DEFINED CONFIG_POUCH_CA_CERT_FILENAME)
        find_file(ca_cert ../../${CONFIG_POUCH_CA_CERT_FILENAME}
            PATHS
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${APP_DIR}
            REQUIRED)

        generate_inc_file_for_target(pouch
            ${ca_cert}
            ${ZEPHYR_BINARY_DIR}/include/generated/golioth_ca_cert.inc)
    endif()

    # Transports

    if (CONFIG_POUCH_TRANSPORT_BLE_GATT)
        add_subdirectory(transport/gatt example_gatt)
    endif()
    if (CONFIG_POUCH_TRANSPORT_HTTP_CLIENT)
        add_subdirectory(transport/http example_http)
    endif()
endif()

if(CONFIG_POUCH_GATEWAY)
    zephyr_include_directories(../../include)
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../.. pouch_core)
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../src/gateway gateway)
endif()

target_link_libraries(app PRIVATE pouch)
