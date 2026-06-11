# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

pouch_config_enabled(_pouch_selected CONFIG_POUCH)

#########################
# Path definitions
#########################

set(POUCH_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
set(POUCH_PORT ${POUCH_ROOT}/port)

#########################
# Pouch Core
#########################

list(APPEND POUCH_CORE_INC_DIRS
    ${POUCH_ROOT}/include
)

#########################
# Port
#########################

list(APPEND POUCH_PORT_INC_DIRS
    ${POUCH_PORT}/esp_idf
    ${POUCH_PORT}/esp_idf/include
    ${POUCH_PORT}/include
    ${POUCH_PORT}/freertos
)

list(APPEND POUCH_CORE_LINKER_FILES
    ${POUCH_PORT}/esp_idf/downlink_handlers.lf
    ${POUCH_PORT}/esp_idf/event_handlers.lf
    ${POUCH_PORT}/esp_idf/uplink_handlers.lf
)

if(_pouch_selected)
    list(APPEND POUCH_PORT_SRCS
        ${POUCH_PORT}/freertos/freertos_port_layer.c
        ${POUCH_PORT}/esp_idf/blockbuf.c
    )

    # Server CA Cert
    if(NOT "${CONFIG_POUCH_CA_CERT_FILENAME}" STREQUAL "")
        find_file(ca_cert ${CONFIG_POUCH_CA_CERT_FILENAME}
            PATHS
            ${CMAKE_CURRENT_LIST_DIR}
            ${POUCH_ROOT}
            REQUIRED)

        message("-- Adding binary ca_cert: ${ca_cert}")

        configure_file(${ca_cert} "${CMAKE_CURRENT_BINARY_DIR}/pouch_ca_cert.der" COPYONLY)
        set(CA_CERT_IF_SELECTED "${CMAKE_CURRENT_BINARY_DIR}/pouch_ca_cert.der")

        list(APPEND POUCH_PORT_SRCS ${POUCH_PORT}/esp_idf/pouch_ca_cert.c)
    endif()
endif()


#########################
# Golioth SDK Linker files
#########################

if("${CONFIG_GOLIOTH}" STREQUAL "y")
    include(${CMAKE_CURRENT_LIST_DIR}/../../golioth_sdk/cmake/golioth_sdk_linker_files.cmake)
    golioth_sdk_get_linker_files_no_ext(_golioth_sdk_linker_files_no_extension)

    foreach(_golioth_sdk_linker_file IN LISTS _golioth_sdk_linker_files_no_extension)
        list(APPEND GOLIOTH_SDK_LINKER_FRAGMENTS ${_golioth_sdk_linker_file}.lf)
    endforeach()

    set(WHOLE_ARCHIVE_IF_SELECTED WHOLE_ARCHIVE)
endif()

#########################
# ESP-IDF Component
#########################

idf_component_register(SRCS
                        ${POUCH_PORT_SRCS}

                       INCLUDE_DIRS
                        ${POUCH_CORE_INC_DIRS}
                        ${POUCH_PORT_INC_DIRS}

                       EMBED_FILES
                        ${CA_CERT_IF_SELECTED}

                       LDFRAGMENTS
                        ${POUCH_CORE_LINKER_FILES}
                        ${GOLIOTH_SDK_LINKER_FRAGMENTS}

                       REQUIRES
                        mbedtls

                       KCONFIG
                        ${POUCH_PORT}/esp_idf/Kconfig

                       ${WHOLE_ARCHIVE_IF_SELECTED})

if(_pouch_selected)
    set(POUCH_ACTIVE_TARGET ${COMPONENT_LIB})
    set(POUCH_ACTIVE_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR})

    # Transports
    add_subdirectory(${POUCH_PORT}/esp_idf/transport transport)

    # Zcbor library (via submodule)
    if(NOT CMAKE_BUILD_EARLY_EXPANSION)
        add_subdirectory(${POUCH_PORT}/esp_idf/lib/zcbor zcbor)
    endif()
endif()
