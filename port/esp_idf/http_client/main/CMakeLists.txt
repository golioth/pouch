# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

set(requires "")
idf_build_get_property(target IDF_TARGET)

idf_component_register(SRCS
                         "main.c"
                         "wifi.c"
                       INCLUDE_DIRS "."
                       )
