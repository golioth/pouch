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

    # Add MbedTLS features through a config file override:
    if (CONFIG_POUCH_ENCRYPTION_SAEAD AND CONFIG_NRF_SECURITY)
        zephyr_library_compile_definitions(MBEDTLS_USER_CONFIG_FILE="${CMAKE_CURRENT_LIST_DIR}/src/saead/mbedtls_config.h")
    endif()

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
        add_subdirectory(src/transport/ble_gatt)
    endif()

    if (DEFINED CONFIG_POUCH_ENCRYPTION_SAEAD AND NOT DEFINED CONFIG_POUCH_VALIDATE_SERVER_CERT)
        message(WARNING " \n"
                " ************************************************\n"
                " Pouch server certificate validation is disabled.\n"
                " Do not use this in production.\n"
                " ************************************************")
    endif()

endif()

# Libraries

add_subdirectory(lib)
