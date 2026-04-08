# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

set(requires "")
idf_build_get_property(target IDF_TARGET)

idf_component_register(SRCS
                         "main.c"
                         "http_client.c"
                         "wifi.c"
                       INCLUDE_DIRS "."
                       EMBED_TXTFILES
                         isrgrootx1_goliothrootx1.pem
                         device.crt.pem
                         device.key.pem
                       )
