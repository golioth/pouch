# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

idf_component_register(SRCS
                         "main.c"
                         "ble_peripheral.c"
                         "credentials.c"

                       INCLUDE_DIRS
                         "."

                       PRIV_REQUIRES
                         bt
                         mbedtls
                         nvs_flash
                         pouch
                       )

# Embed device certificate and private key (DER format)
target_add_binary_data(${COMPONENT_LIB} "device.crt.der" BINARY)
target_add_binary_data(${COMPONENT_LIB} "device.key.der" BINARY)
