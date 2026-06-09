# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

if(NOT "${CONFIG_POUCH_TRANSPORT_HTTP_CLIENT}" STREQUAL "y")
    return()
endif()

add_library(pouch_transport_http)
target_link_libraries(${COMPONENT_LIB} PRIVATE pouch_transport_http)

target_sources(pouch_transport_http PRIVATE
    client.c
)

target_include_directories(pouch_transport_http PRIVATE
    ${POUCH_ROOT}/include
    ${POUCH_ROOT}/port/include
    ${POUCH_ROOT}/src
    ${POUCH_ROOT}/port/esp_idf
    ${POUCH_ROOT}/port/esp_idf/include
)

target_link_libraries(pouch_transport_http PUBLIC
    idf::esp_http_client
    idf::esp-tls
)
