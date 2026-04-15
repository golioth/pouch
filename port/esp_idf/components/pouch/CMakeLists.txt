# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

#########################
# Path definitions
#########################

set(POUCH_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../../..)
set(POUCH_PORT ${POUCH_ROOT}/port)
set(POUCH_LIB ${POUCH_ROOT}/lib)
set(POUCH_TRANSPORT_COMMON ${POUCH_ROOT}/src/transport)

#########################
# Port
#########################

list(APPEND POUCH_PORT_INC_DIRS
    ${POUCH_PORT}/esp_idf  # esp_idf/pouch_port_macros_internal.h
    ${POUCH_PORT}/include  # pouch/port.h
    ${POUCH_PORT}/freertos # port/freertos
)

list(APPEND POUCH_PORT_SRCS
    ${POUCH_PORT}/freertos/freertos_port_layer.c
    ${POUCH_PORT}/esp_idf/pouch_ca_cert.c
    ${POUCH_PORT}/esp_idf/blockbuf.c
)

#########################
# Pouch Core
#########################

file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/../include)
list(APPEND POUCH_CORE_INC_DIRS
      ${POUCH_ROOT}/include

      # Generated Headers
      ${CMAKE_CURRENT_BINARY_DIR}/../include
)

list(APPEND POUCH_CORE_SRCS
      ${POUCH_ROOT}/src/pouch.c
      ${POUCH_ROOT}/src/uplink.c
      ${POUCH_ROOT}/src/header.c
      ${POUCH_ROOT}/src/buf.c
      ${POUCH_ROOT}/src/block.c
      ${POUCH_ROOT}/src/entry.c
      ${POUCH_ROOT}/src/stream.c
      ${POUCH_ROOT}/src/downlink.c

      # Generated Sources
      ${CMAKE_CURRENT_BINARY_DIR}/header_decode.c
      ${CMAKE_CURRENT_BINARY_DIR}/header_encode.c
  )

list(APPEND POUCH_CORE_LINKER_FILES
    ${POUCH_PORT}/esp_idf/downlink_handlers.lf
    ${POUCH_PORT}/esp_idf/event_handlers.lf
    ${POUCH_PORT}/esp_idf/uplink_handlers.lf
)

if ("${CONFIG_POUCH_ENCRYPTION_NONE}" STREQUAL "y")
    list(APPEND POUCH_CORE_SRCS ${POUCH_ROOT}/src/crypto_none.c)
endif()

if ("${CONFIG_POUCH_ENCRYPTION_SAEAD}" STREQUAL "y")
    list(APPEND POUCH_CORE_SRCS
        ${POUCH_ROOT}/src/crypto_saead.c
        ${POUCH_ROOT}/src/cert.c
        ${POUCH_ROOT}/src/saead/session.c
        ${POUCH_ROOT}/src/saead/uplink.c
        ${POUCH_ROOT}/src/saead/downlink.c
    )
endif()

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    add_custom_command(
        OUTPUT
            ${CMAKE_CURRENT_BINARY_DIR}/header_decode.c
            ${CMAKE_CURRENT_BINARY_DIR}/header_encode.c
        COMMAND zcbor code -c ${POUCH_ROOT}/src/header.cddl -t pouch_header -sde
            --include-prefix cddl/ --oc header.c --oh ../include/cddl/header.h
        BYPRODUCTS
            ${CMAKE_CURRENT_BINARY_DIR}/include/cddl/header_decode.h
            ${CMAKE_CURRENT_BINARY_DIR}/include/cddl/header_encode.h
            ${CMAKE_CURRENT_BINARY_DIR}/include/cddl/header_types.h
        DEPENDS ${POUCH_ROOT}/src/header.cddl)

    add_custom_target(pouch_generate_headers DEPENDS
        ${CMAKE_CURRENT_BINARY_DIR}/header_decode.c
        ${CMAKE_CURRENT_BINARY_DIR}/header_encode.c)
endif()

if ("${CONFIG_POUCH_ENCRYPTION_SAEAD}" STREQUAL "y")
    if (NOT "${CONFIG_POUCH_VALIDATE_SERVER_CERT}" STREQUAL "y")
        message(WARNING " \n"
                " ************************************************\n"
                " Pouch server certificate validation is disabled.\n"
                " Do not use this in production.\n"
                " ************************************************")
    endif()

    if ("${CONFIG_MBEDTLS_PSA_P256M_DRIVER_ENABLED}" STREQUAL "y")
        message(FATAL_ERROR " \n"
                " **************************************************\n"
                " CONFIG_MBEDTLS_PSA_256M_DRIVER_ENABLED breaks\n"
                " support for the secp384r1 curve, which is required\n"
                " for Pouch. Disable this option in sdkconfig\n"
                " **************************************************")
    endif()
endif()

#########################
# Transport Common
#########################

# TODO: Add port-specific transports and include them in the build here

if("${CONFIG_POUCH_TRANSPORT_BLE_GATT}" STREQUAL "y")

    list(APPEND POUCH_TRANSPORT_SRCS
        ${POUCH_TRANSPORT_COMMON}/endpoints/info.c
        ${POUCH_TRANSPORT_COMMON}/endpoints/uplink.c
        ${POUCH_TRANSPORT_COMMON}/endpoints/downlink.c
        ${POUCH_TRANSPORT_COMMON}/sar/sender.c
        ${POUCH_TRANSPORT_COMMON}/sar/receiver.c
        ${POUCH_TRANSPORT_COMMON}/sar/packet.c
        ${CMAKE_CURRENT_BINARY_DIR}/info_encode.c
    )

    if ("${CONFIG_POUCH_ENCRYPTION_SAEAD}" STREQUAL "y")
        list(APPEND POUCH_TRANSPORT_SRCS
            ${POUCH_TRANSPORT_COMMON}/endpoints/device_cert.c
            ${POUCH_TRANSPORT_COMMON}/endpoints/server_cert.c
        )
    endif()

    if(NOT CMAKE_BUILD_EARLY_EXPANSION)

        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/info_encode.c

            COMMAND zcbor code -c ${POUCH_TRANSPORT_COMMON}/info.cddl -t pouch_gatt_info -e
            --include-prefix cddl/ --output-c ${CMAKE_CURRENT_BINARY_DIR}/info_encode.c --output-h
            ${CMAKE_CURRENT_BINARY_DIR}/include/cddl/info_encode.h

            BYPRODUCTS include/cddl/info_encode.h include/cddl/info_encode_types.h

            DEPENDS ${POUCH_TRANSPORT_COMMON}/info.cddl)

        list(APPEND POUCH_TRANSPORT_INC_DIRS
            ${POUCH_TRANSPORT_COMMON}
            ${CMAKE_CURRENT_BINARY_DIR}/include
        )

    endif()

endif()

#########################
# Libraries
#########################

if("${CONFIG_ZCBOR_UTILS}" STREQUAL "y")
    list(APPEND POUCH_LIB_INC_DIRS ${POUCH_LIB}/zcbor_utils/include)
    list(APPEND POUCH_LIB_SRCS ${POUCH_LIB}/zcbor_utils/zcbor_utils.c)
endif()

#########################
# Server CA Cert
#########################

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

#########################
# ESP-IDF Component
#########################

idf_component_register(SRCS
                        ${POUCH_CORE_SRCS}
                        ${POUCH_LIB_SRCS}
                        ${POUCH_PORT_SRCS}
                        ${POUCH_TRANSPORT_SRCS}

                       INCLUDE_DIRS
                        ${POUCH_CORE_INC_DIRS}
                        ${POUCH_LIB_INC_DIRS}
                        ${POUCH_PORT_INC_DIRS}
                        ${POUCH_TRANSPORT_INC_DIRS}

                       EMBED_FILES
                        ${CA_CERT_IF_SELECTED}

                       LDFRAGMENTS
                        ${POUCH_CORE_LINKER_FILES}

                       REQUIRES
                         mbedtls
                         zcbor
                       )
