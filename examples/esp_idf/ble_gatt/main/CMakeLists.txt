# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

idf_component_register(SRCS
                         "main.c"
                         "ble_peripheral.c"
                         "console.c"
                         "credentials.c"

                       INCLUDE_DIRS
                         "."

                       PRIV_REQUIRES
                         bt
                         console
                         mbedtls
                         nvs_flash
                         pouch
                       )
