# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

set(POUCH_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
set(POUCH_PORT ${POUCH_ROOT}/port)

include(${POUCH_ROOT}/cmake/pouch_target.cmake)

pouch_config_enabled(_pouch_selected CONFIG_POUCH)

if(ESP_PLATFORM AND CMAKE_BUILD_EARLY_EXPANSION AND NOT DEFINED CONFIG_POUCH)
    # During ESP-IDF early expansion, CONFIG_* values may be unavailable while
    # component requirements are being enumerated. Default CONFIG_POUCH to
    # enabled in this phase so REQUIRES dependencies (for example mbedtls) are
    # captured for the pouch component.
    set(_pouch_selected TRUE)
endif()

list(APPEND POUCH_PORT_INC_DIRS
    ${POUCH_PORT}/esp_idf
    ${POUCH_PORT}/esp_idf/include
    ${POUCH_PORT}/include
    ${POUCH_PORT}/freertos
)

list(APPEND POUCH_PORT_SRCS
    ${POUCH_PORT}/freertos/freertos_port_layer.c
    ${POUCH_PORT}/esp_idf/pouch_ca_cert.c
    ${POUCH_PORT}/esp_idf/blockbuf.c
)

list(APPEND POUCH_CORE_INC_DIRS
    ${POUCH_ROOT}/include
)

list(APPEND POUCH_CORE_LINKER_FILES
    ${POUCH_PORT}/esp_idf/downlink_handlers.lf
    ${POUCH_PORT}/esp_idf/event_handlers.lf
    ${POUCH_PORT}/esp_idf/uplink_handlers.lf
)

if(_pouch_selected)
    if(NOT "${CONFIG_POUCH_CA_CERT_FILENAME}" STREQUAL "")
        find_file(ca_cert ${CONFIG_POUCH_CA_CERT_FILENAME}
            PATHS
            ${CMAKE_CURRENT_LIST_DIR}
            ${POUCH_ROOT}
            REQUIRED)

        message("-- Adding binary ca_cert: ${ca_cert}")

        configure_file(${ca_cert} "${CMAKE_CURRENT_BINARY_DIR}/pouch_ca_cert.der" COPYONLY)
        set(CA_CERT_IF_SELECTED "${CMAKE_CURRENT_BINARY_DIR}/pouch_ca_cert.der")
    endif()

    idf_component_register(SRCS
                            ${POUCH_PORT_SRCS}

                           INCLUDE_DIRS
                            ${POUCH_CORE_INC_DIRS}
                            ${POUCH_PORT_INC_DIRS}

                           EMBED_FILES
                            ${CA_CERT_IF_SELECTED}

                            LDFRAGMENTS
                              ${POUCH_CORE_LINKER_FILES}

                           REQUIRES
                            mbedtls

                            KCONFIG
                             ${POUCH_PORT}/esp_idf/Kconfig
                           )

    set(POUCH_ACTIVE_TARGET ${COMPONENT_LIB})
    set(POUCH_ACTIVE_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR})

    add_subdirectory(${POUCH_PORT}/esp_idf/transport transport)

    if(NOT CMAKE_BUILD_EARLY_EXPANSION)
        add_subdirectory(${POUCH_PORT}/esp_idf/lib/zcbor zcbor)
    endif()

else()
    idf_component_register(KCONFIG
                           ${POUCH_PORT}/esp_idf/Kconfig)
endif()
