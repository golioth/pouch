# Copyright (c) 2025 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_POUCH)
    zephyr_include_directories(include)

    zephyr_library_named(pouch)

    zephyr_library_sources(
      ${CMAKE_CURRENT_BINARY_DIR}/header_decode.c
      ${CMAKE_CURRENT_BINARY_DIR}/header_encode.c
      src/pouch.c
      src/uplink.c
      src/header.c
      src/buf.c
      src/block.c
      src/entry.c
      src/stream.c
      src/downlink.c
    )

    zephyr_library_sources_ifdef(CONFIG_POUCH_ENCRYPTION_NONE src/crypto_none.c)
    zephyr_library_sources_ifdef(CONFIG_POUCH_ENCRYPTION_SAEAD
        src/crypto_saead.c
        src/cert.c
        src/saead/session.c
        src/saead/uplink.c
        src/saead/downlink.c
    )

    if (DEFINED CONFIG_POUCH_CA_CERT_FILENAME)
        find_file(ca_cert ${CONFIG_POUCH_CA_CERT_FILENAME}
            PATHS
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${APP_DIR}
            REQUIRED)

        generate_inc_file_for_target(pouch
            ${ca_cert}
            ${ZEPHYR_BINARY_DIR}/include/generated/golioth_ca_cert.inc)
    endif()

    zephyr_linker_sources(SECTIONS src/downlink_handlers.ld)
    zephyr_linker_sources(SECTIONS src/event_handlers.ld)

    add_custom_command(OUTPUT header_decode.c header_encode.c
        COMMAND zcbor code -c ${CMAKE_CURRENT_LIST_DIR}/src/header.cddl -t pouch_header -sde --include-prefix cddl/ --oc header.c --oh include/cddl/header.h
        BYPRODUCTS include/cddl/header_decode.h include/cddl/header_encode.h include/cddl/header_types.h
        DEPENDS src/header.cddl)

    zephyr_library_include_directories(${CMAKE_CURRENT_BINARY_DIR}/include)

    zephyr_library_link_libraries(mbedTLS)

    # Transports

    if (CONFIG_POUCH_TRANSPORT_BLE_GATT)
        add_subdirectory(src/transport/gatt)
    endif()
    if (CONFIG_POUCH_TRANSPORT_COAP)
        add_subdirectory(src/transport/coap)
    endif()

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

    add_subdirectory(golioth_sdk)

endif()

# Libraries

add_subdirectory(lib)
