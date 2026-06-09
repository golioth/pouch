# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

set(POUCH_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
set(POUCH_PORT ${POUCH_ROOT}/port)
set(POUCH_LIB ${POUCH_ROOT}/lib)
set(POUCH_TRANSPORT_COMMON ${POUCH_ROOT}/src/transport)
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


if("${CONFIG_POUCH_TRANSPORT_BLE_GATT}" STREQUAL "y")
    list(APPEND POUCH_TRANSPORT_SRCS
        ${POUCH_TRANSPORT_COMMON}/endpoints/device/info.c
        ${POUCH_TRANSPORT_COMMON}/endpoints/device/uplink.c
        ${POUCH_TRANSPORT_COMMON}/endpoints/device/downlink.c
        ${POUCH_TRANSPORT_COMMON}/sar/sender.c
        ${POUCH_TRANSPORT_COMMON}/sar/receiver.c
        ${POUCH_TRANSPORT_COMMON}/sar/packet.c
        ${CMAKE_CURRENT_BINARY_DIR}/info_encode.c
    )

    if("${CONFIG_POUCH_ENCRYPTION_SAEAD}" STREQUAL "y")
        list(APPEND POUCH_TRANSPORT_SRCS
            ${POUCH_TRANSPORT_COMMON}/endpoints/device/device_cert.c
            ${POUCH_TRANSPORT_COMMON}/endpoints/device/server_cert.c
        )
    endif()

    list(APPEND POUCH_TRANSPORT_INC_DIRS
        ${POUCH_ROOT}/src
        ${POUCH_TRANSPORT_COMMON}
        ${CMAKE_CURRENT_BINARY_DIR}/include
    )

    if(NOT CMAKE_BUILD_EARLY_EXPANSION)
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/info_encode.c
            COMMAND zcbor code -c ${POUCH_TRANSPORT_COMMON}/info.cddl -t pouch_gatt_info -e
            --include-prefix cddl/ --output-c ${CMAKE_CURRENT_BINARY_DIR}/info_encode.c --output-h
            ${CMAKE_CURRENT_BINARY_DIR}/include/cddl/info_encode.h
            BYPRODUCTS include/cddl/info_encode.h include/cddl/info_encode_types.h
            DEPENDS ${POUCH_TRANSPORT_COMMON}/info.cddl)
    endif()
endif()

if("${CONFIG_POUCH_TRANSPORT_HTTP_CLIENT}" STREQUAL "y")
    list(APPEND POUCH_TRANSPORT_SRCS ${POUCH_PORT}/esp_idf/transport/http/client.c)
    list(APPEND POUCH_PRIV_REQUIRES
        esp_http_client
        esp-tls
    )
endif()

if("${CONFIG_POUCH_TRANSPORT_BLE_GATT}" STREQUAL "y")
    list(APPEND POUCH_TRANSPORT_SRCS ${POUCH_PORT}/esp_idf/transport/gatt/peripheral.c)
    list(APPEND POUCH_PRIV_REQUIRES
        bt
    )
endif()

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
                        ${POUCH_TRANSPORT_SRCS}
                        ${GOLIOTH_SDK_SRCS}

                       INCLUDE_DIRS
                        ${POUCH_CORE_INC_DIRS}
                        ${POUCH_LIB_INC_DIRS}
                        ${POUCH_PORT_INC_DIRS}
                        ${POUCH_TRANSPORT_INC_DIRS}
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

idf_component_optional_requires(PRIVATE ${POUCH_PRIV_REQUIRES})

if("${CONFIG_GOLIOTH}" STREQUAL "y")
    idf_component_set_property(${COMPONENT_NAME} WHOLE_ARCHIVE TRUE)
endif()

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    add_subdirectory(${POUCH_PORT}/esp_idf/lib/zcbor zcbor)
    target_link_libraries(${COMPONENT_LIB} PUBLIC zcbor)
endif()
