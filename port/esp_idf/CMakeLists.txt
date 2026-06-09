# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

set(POUCH_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
set(POUCH_PORT ${POUCH_ROOT}/port)
set(POUCH_LIB ${POUCH_ROOT}/lib)
set(GOLIOTH_SDK_ROOT ${POUCH_ROOT}/golioth_sdk)

include(${POUCH_ROOT}/cmake/pouch_target.cmake)

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
    ${CMAKE_CURRENT_BINARY_DIR}/include
)

list(APPEND POUCH_CORE_LINKER_FILES
    ${POUCH_PORT}/esp_idf/downlink_handlers.lf
    ${POUCH_PORT}/esp_idf/event_handlers.lf
    ${POUCH_PORT}/esp_idf/uplink_handlers.lf
)


if("${CONFIG_ZCBOR_UTILS}" STREQUAL "y")
    list(APPEND POUCH_LIB_INC_DIRS ${POUCH_LIB}/zcbor_utils/include)
    list(APPEND POUCH_LIB_SRCS ${POUCH_LIB}/zcbor_utils/zcbor_utils.c)
endif()

if("${CONFIG_GOLIOTH}" STREQUAL "y")
    list(APPEND GOLIOTH_SDK_SRCS ${GOLIOTH_SDK_ROOT}/dispatch.c)
    list(APPEND GOLIOTH_SDK_LINKER_FILES ${GOLIOTH_SDK_ROOT}/dispatch.lf)

    if("${CONFIG_GOLIOTH_SETTINGS}" STREQUAL "y")
        list(APPEND GOLIOTH_SDK_SRCS
            ${GOLIOTH_SDK_ROOT}/settings.c
            ${GOLIOTH_SDK_ROOT}/settings_callbacks.c
        )
        list(APPEND GOLIOTH_SDK_LINKER_FILES ${GOLIOTH_SDK_ROOT}/settings_callbacks.lf)
    endif()

    if("${CONFIG_GOLIOTH_OTA}" STREQUAL "y")
        list(APPEND GOLIOTH_SDK_SRCS
            ${GOLIOTH_SDK_ROOT}/hex.c
            ${GOLIOTH_SDK_ROOT}/ota.c
            ${GOLIOTH_SDK_ROOT}/ota_upper.c
        )
        list(APPEND GOLIOTH_SDK_LINKER_FILES ${GOLIOTH_SDK_ROOT}/ota.lf)
    endif()

    list(APPEND GOLIOTH_SDK_INC_DIRS ${GOLIOTH_SDK_ROOT})
endif()

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
                        ${POUCH_LIB_SRCS}
                        ${POUCH_PORT_SRCS}
                        ${GOLIOTH_SDK_SRCS}

                       INCLUDE_DIRS
                        ${POUCH_CORE_INC_DIRS}
                        ${POUCH_LIB_INC_DIRS}
                        ${POUCH_PORT_INC_DIRS}
                        ${GOLIOTH_SDK_INC_DIRS}

                       EMBED_FILES
                        ${CA_CERT_IF_SELECTED}

                       LDFRAGMENTS
                        ${POUCH_CORE_LINKER_FILES}
                        ${GOLIOTH_SDK_LINKER_FILES}

                       REQUIRES
                        mbedtls

                        KCONFIG
                         ${POUCH_PORT}/esp_idf/Kconfig
                       )

pouch_target_add_core_sources(${COMPONENT_LIB} ${POUCH_ROOT})
pouch_target_add_header_codegen(${COMPONENT_LIB} ${POUCH_ROOT} ${CMAKE_CURRENT_BINARY_DIR})
pouch_validate_config()

add_subdirectory(${POUCH_PORT}/esp_idf/transport transport)

if(TARGET pouch_transport_http)
    target_link_libraries(${COMPONENT_LIB} PRIVATE pouch_transport_http)
endif()

if(TARGET pouch_transport_gatt)
    target_link_libraries(${COMPONENT_LIB} PRIVATE pouch_transport_gatt)
endif()

if("${CONFIG_GOLIOTH}" STREQUAL "y")
    idf_component_set_property(${COMPONENT_NAME} WHOLE_ARCHIVE TRUE)
endif()

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    add_subdirectory(${POUCH_PORT}/esp_idf/lib/zcbor zcbor)
    target_link_libraries(${COMPONENT_LIB} PUBLIC zcbor)
endif()
