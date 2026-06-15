# Copyright (c) 2026 Golioth, Inc.
# SPDX-License-Identifier: Apache-2.0

file(GLOB TEST_SRCS "*.c")

idf_component_register(SRCS
                        ${TEST_SRCS}
                       PRIV_REQUIRES
                        mbedtls
                        pouch
                        unity
                       LDFRAGMENTS
                        ${CMAKE_CURRENT_LIST_DIR}/test_iterable_section.lf
                       WHOLE_ARCHIVE)
