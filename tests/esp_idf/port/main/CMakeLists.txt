# Copyright (c) 2026 Golioth, Inc.
# SPDX-License-Identifier: Apache-2.0

file(GLOB TEST_SRCS "../tests/*.c")

idf_component_register(SRCS
                        "unit_test.c"
                        ${TEST_SRCS}
                       WHOLE_ARCHIVE)
