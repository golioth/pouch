# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_POUCH)
    set(POUCH_COMMON_INC_DIRS
        ${CMAKE_CURRENT_LIST_DIR}/include
    )

    set(POUCH_COMMON_SRCS
        ${CMAKE_CURRENT_BINARY_DIR}/header_decode.c
        ${CMAKE_CURRENT_BINARY_DIR}/header_encode.c
        ${CMAKE_CURRENT_LIST_DIR}/src/pouch.c
        ${CMAKE_CURRENT_LIST_DIR}/src/uplink.c
        ${CMAKE_CURRENT_LIST_DIR}/src/header.c
        ${CMAKE_CURRENT_LIST_DIR}/src/buf.c
        ${CMAKE_CURRENT_LIST_DIR}/src/block.c
        ${CMAKE_CURRENT_LIST_DIR}/src/entry.c
        ${CMAKE_CURRENT_LIST_DIR}/src/stream.c
        ${CMAKE_CURRENT_LIST_DIR}/src/downlink.c
    )

    if (CONFIG_POUCH_ENCRYPTION_NONE)
        list(APPEND POUCH_COMMON_SRCS ${CMAKE_CURRENT_LIST_DIR}/src/crypto_none.c)
    endif (CONFIG_POUCH_ENCRYPTION_NONE)

    if (CONFIG_POUCH_ENCRYPTION_SAEAD)
        list(APPEND POUCH_COMMON_SRCS
            ${CMAKE_CURRENT_LIST_DIR}/src/crypto_saead.c
            ${CMAKE_CURRENT_LIST_DIR}/src/cert.c
            ${CMAKE_CURRENT_LIST_DIR}/src/saead/session.c
            ${CMAKE_CURRENT_LIST_DIR}/src/saead/uplink.c
            ${CMAKE_CURRENT_LIST_DIR}/src/saead/downlink.c
        )
    endif (CONFIG_POUCH_ENCRYPTION_SAEAD)

    #target_sources(pouch PRIVATE ${POUCH_COMMON_SRCS})

    add_custom_command(
        OUTPUT
            ${CMAKE_CURRENT_BINARY_DIR}/header_decode.c
            ${CMAKE_CURRENT_BINARY_DIR}/header_encode.c
        COMMAND zcbor code -c ${CMAKE_CURRENT_LIST_DIR}/src/header.cddl -t pouch_header -sde
            --include-prefix cddl/ --oc header.c --oh ../include/cddl/header.h
        BYPRODUCTS
            ${CMAKE_CURRENT_BINARY_DIR}/include/cddl/header_decode.h
            ${CMAKE_CURRENT_BINARY_DIR}/include/cddl/header_encode.h
            ${CMAKE_CURRENT_BINARY_DIR}/include/cddl/header_types.h
        DEPENDS ${CMAKE_CURRENT_LIST_DIR}/src/header.cddl)

    add_custom_target(pouch_generate_headers DEPENDS
        ${CMAKE_CURRENT_BINARY_DIR}/header_decode.c
        ${CMAKE_CURRENT_BINARY_DIR}/header_encode.c)

    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/../include)
    list(APPEND POUCH_COMMON_INC_DIRS
        ${CMAKE_CURRENT_BINARY_DIR}/../include
    )

    if (DEFINED CONFIG_POUCH_ENCRYPTION_SAEAD AND NOT DEFINED CONFIG_POUCH_VALIDATE_SERVER_CERT)
        message(WARNING " \n"
                " ************************************************\n"
                " Pouch server certificate validation is disabled.\n"
                " Do not use this in production.\n"
                " ************************************************")
    endif()

    if (DEFINED CONFIG_POUCH_ENCRYPTION_SAEAD AND DEFINED CONFIG_MBEDTLS_PSA_P256M_DRIVER_ENABLED)
        message(FATAL_ERROR " \n"
                " **************************************************\n"
                " CONFIG_MBEDTLS_PSA_256M_DRIVER_ENABLED breaks\n"
                " support for the secp384r1 curve, which is required\n"
                " for Pouch. Disable this option in prj.conf\n"
                " **************************************************")
    endif()

    set(SDK_NAME_LIST)
    include("${CMAKE_CURRENT_LIST_DIR}/golioth_sdk/golioth_sdk.cmake")

endif()

# Libraries

set(POUCH_LIB_INC_DIRS)
set(POUCH_LIB_SRCS)
include(${CMAKE_CURRENT_LIST_DIR}/lib/libraries.cmake)
