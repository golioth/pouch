# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

list(APPEND EXAMPLE_SRCS
        "credentials.c"
        "main.c"
)

list(APPEND EXAMPLE_EMBEDS
        "test/placeholder_device.crt.der"
        "test/placeholder_device.key.der"
)

idf_component_register(SRCS
                         ${EXAMPLE_SRCS}

                       EMBED_FILES
                         ${EXAMPLE_EMBEDS}

                       REQUIRES
                         pouch
                       )
