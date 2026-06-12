# Copyright (c) 2026 Golioth, Inc.
# SPDX-License-Identifier: Apache-2.0

idf_component_register(SRCS
                        "unit_test.c"

                       PRIV_REQUIRES
                        pouch
                        tests
                        unity

                       WHOLE_ARCHIVE)
