# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

list(APPEND EXAMPLE_SRCS
        "credentials.c"
        "main.c"
        "wifi.c"
)

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    if(EXISTS "${CMAKE_SOURCE_DIR}/${CONFIG_SERVER_CA_CERT_PEM_FILE}")
        configure_file("${CMAKE_SOURCE_DIR}/${CONFIG_SERVER_CA_CERT_PEM_FILE}"
                    "${CMAKE_CURRENT_BINARY_DIR}/server_ca_cert.pem" COPYONLY)

        list(APPEND EXAMPLE_TXT_EMBEDS "${CMAKE_CURRENT_BINARY_DIR}/server_ca_cert.pem")
    else()
        message(WARNING "Server CA certificate file not found:
                        ${CMAKE_SOURCE_DIR}/${CONFIG_SERVER_CA_CERT_PEM_FILE}")
    endif()
endif()

list(APPEND EXAMPLE_EMBEDS
        "test/placeholder_device.crt.der"
        "test/placeholder_device.key.der"
)

idf_component_register(SRCS
                         ${EXAMPLE_SRCS}

                       EMBED_FILES
                         ${EXAMPLE_EMBEDS}

                       EMBED_TXTFILES
                         ${EXAMPLE_TXT_EMBEDS}

                       REQUIRES
                         esp_wifi
                         nvs_flash
                         pouch
                       )
