# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

zephyr_include_directories(../include)      # pouch/port.h
zephyr_include_directories(include)         # Zephyr port includes
zephyr_library_sources(zephyr_port_layer.c) # Port functions

if(CONFIG_POUCH)

    zephyr_include_directories(../../include) # Pouch includes

    zephyr_library_named(pouch)
    set(POUCH_ACTIVE_TARGET ${ZEPHYR_CURRENT_LIBRARY})

    zephyr_linker_sources(SECTIONS downlink_handlers.ld)
    zephyr_linker_sources(SECTIONS uplink_handlers.ld)
    zephyr_linker_sources(SECTIONS event_handlers.ld)

    # Golioth_SDK linker sources
    include(${CMAKE_CURRENT_LIST_DIR}/../../golioth_sdk/cmake/golioth_sdk_linker_files.cmake)
    golioth_sdk_get_linker_files_no_ext(_golioth_sdk_linker_files_no_extension)
    foreach(_golioth_sdk_linker_file IN LISTS _golioth_sdk_linker_files_no_extension)
        zephyr_linker_sources(SECTIONS ${_golioth_sdk_linker_file}.ld)
    endforeach()

    if (DEFINED CONFIG_POUCH_CA_CERT_FILENAME)
        find_file(ca_cert ../../${CONFIG_POUCH_CA_CERT_FILENAME}
            PATHS
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${APP_DIR}
            REQUIRED)

        generate_inc_file_for_target(pouch
            ${ca_cert}
            ${ZEPHYR_BINARY_DIR}/include/generated/pouch_ca_cert.inc)

        zephyr_library_sources(pouch_ca_cert.c)
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
    if (CONFIG_POUCH_TRANSPORT_COAP_CLIENT)
        add_subdirectory(transport/coap example_coap)
    endif()
endif()

if(CONFIG_POUCH_GATEWAY)
    zephyr_include_directories(../../include)
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../.. core)
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../src/gateway gateway)
endif()
