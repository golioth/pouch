# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_POUCH)
    target_include_directories(pouch PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/include)

    set(pouch_source_files)

    list(APPEND pouch_source_files
      src/pouch.c
      src/uplink.c
      src/header.c
      src/buf.c
      src/block.c
      src/entry.c
      src/stream.c
      src/downlink.c
    )

    if (CONFIG_POUCH_ENCRYPTION_NONE)
        list(APPEND pouch_source_files src/crypto_none.c)
    endif (CONFIG_POUCH_ENCRYPTION_NONE)

    if (CONFIG_POUCH_ENCRYPTION_SAEAD)
        list(APPEND pouch_source_files
            src/crypto_saead.c
            src/cert.c
            src/saead/session.c
            src/saead/uplink.c
            src/saead/downlink.c
        )
    endif (CONFIG_POUCH_ENCRYPTION_SAEAD)

    target_sources(pouch PRIVATE ${pouch_source_files})

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

    add_dependencies(pouch pouch_generate_headers)

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

    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/golioth_sdk golioth_sdk)

endif()

# Libraries

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/lib lib)
