# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

idf_component_register(SRCS
                         "main.c"
                         "ble_peripheral.c"
                         "console.c"
                         "credentials.c"
                         "fw_update.c"

                       INCLUDE_DIRS
                         "."

                       PRIV_REQUIRES
                         app_update
                         bt
                         console
                         mbedtls
                         nvs_flash
                         pouch
                       )

# fw_update.c has no global references, use WHOLE_ARCHIVE to ensure it gets processed by the linker
idf_component_set_property(${COMPONENT_NAME} WHOLE_ARCHIVE TRUE)
