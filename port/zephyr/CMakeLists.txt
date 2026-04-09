# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

zephyr_library_named(pouch)

include("../../pouch_common.cmake")

zephyr_include_directories(../include)      # pouch/port.h
zephyr_include_directories(include)         # Zephyr port includes
zephyr_library_sources(zephyr_port_layer.c) # Port functions

if(CONFIG_POUCH)

    if(POUCH_COMMON_INC_DIRS)
        zephyr_include_directories(${POUCH_COMMON_INC_DIRS}) # Pouch includes
    endif()

    if(POUCH_LIB_INC_DIRS)
        zephyr_include_directories(${POUCH_LIB_INC_DIRS})    # Pouch library includes
    endif()

    if(POUCH_COMMON_SRCS)
        zephyr_library_sources(${POUCH_COMMON_SRCS}) # Pouch sources
    endif()

    if(POUCH_LIB_SRCS)
        zephyr_library_sources(${POUCH_LIB_SRCS})    # Pouch library sources
    endif()

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

    zephyr_library_sources(
        ${CMAKE_CURRENT_BINARY_DIR}/header_decode.c
        ${CMAKE_CURRENT_BINARY_DIR}/header_encode.c
    )

    zephyr_library_link_libraries(mbedTLS)

    # Transports

    if (CONFIG_POUCH_TRANSPORT_BLE_GATT)
        add_subdirectory(transport/gatt example_gatt)
    endif()
    if (CONFIG_POUCH_TRANSPORT_HTTP_CLIENT)
        add_subdirectory(transport/http example_http)
    endif()
endif()

# SDKs

# WARNING: This block of code switches the Zephyr library context away from `pouch`
if (SDK_NAME_LIST)
    foreach(SDK_NAME ${SDK_NAME_LIST})

        zephyr_library_named("${SDK_NAME}")

        if (${SDK_NAME}_SRCS)
            zephyr_library_sources(${${SDK_NAME}_SRCS})
        endif (${SDK_NAME}_SRCS)

        if (${SDK_NAME}_INC_DIRS)
            zephyr_library_include_directories(${${SDK_NAME}_INC_DIRS})
        endif (${SDK_NAME}_INC_DIRS)

        if (${SDK_NAME}_LINKER_SECTION_ZEPHYR)
            foreach(LINKER_FILE ${${SDK_NAME}_LINKER_SECTION_ZEPHYR})
                message(WARNING "################# ${LINKER_FILE}")
                zephyr_linker_sources(SECTIONS ${LINKER_FILE})
            endforeach(LINKER_FILE)
        endif (${SDK_NAME}_LINKER_SECTION_ZEPHYR)

    endforeach(SDK_NAME)
endif (SDK_NAME_LIST)

if(CONFIG_POUCH_GATEWAY)
    zephyr_include_directories(../../include)
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../.. core)
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../src/gateway gateway)
endif()
