# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

zephyr_include_directories(../include)      # pouch/port.h
zephyr_include_directories(include)         # Zephyr port includes
zephyr_library_sources(zephyr_port_layer.c) # Port functions

if(CONFIG_POUCH)

    zephyr_include_directories(../../include) # Pouch includes

    zephyr_library_named(pouch)

    zephyr_linker_sources(SECTIONS downlink_handlers.ld)
    zephyr_linker_sources(SECTIONS uplink_handlers.ld)
    zephyr_linker_sources(SECTIONS event_handlers.ld)

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

    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../.. core)

    zephyr_library_sources(
        ${CMAKE_CURRENT_BINARY_DIR}/core/header_decode.c
        ${CMAKE_CURRENT_BINARY_DIR}/core/header_encode.c
        ${CMAKE_CURRENT_SOURCE_DIR}/blockbuf.c
    )

    zephyr_library_include_directories(${CMAKE_CURRENT_BINARY_DIR}/include)
    zephyr_library_include_directories(${CMAKE_CURRENT_LIST_DIR}/../../src)

    zephyr_library_link_libraries(mbedTLS)

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
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../.. core)
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../src/gateway gateway)
endif()
