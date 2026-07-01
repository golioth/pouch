# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

pouch_config_enabled(_pouch_transport_http_client CONFIG_POUCH_TRANSPORT_HTTP_CLIENT)
if(NOT _pouch_transport_http_client)
    return()
endif()

target_sources(${POUCH_ACTIVE_TARGET} PRIVATE client.c)

target_include_directories(${POUCH_ACTIVE_TARGET} PRIVATE
    # Access to shared headers like: #include "transport/sar/receiver.h"
    ${POUCH_ROOT}/src
)
