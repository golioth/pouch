# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

list(APPEND EXAMPLE_SRCS
        "console.c"
        "credentials.c"
        "credentials_nvs.c"
        "fw_update.c"
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

idf_component_register(SRCS
                         ${EXAMPLE_SRCS}

                       EMBED_TXTFILES
                         ${EXAMPLE_TXT_EMBEDS}

                       PRIV_REQUIRES
                         console

                       REQUIRES
                         esp_wifi
                         nvs_flash
                         pouch
                         golioth_sdk
                         app_update
                       )

# fw_update.c has no global references, use WHOLE_ARCHIVE to ensure it gets processed by the linker
idf_component_set_property(${COMPONENT_NAME} WHOLE_ARCHIVE TRUE)
