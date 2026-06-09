# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

if(ESP_PLATFORM)
    include(${CMAKE_CURRENT_LIST_DIR}/port/esp_idf/CMakeLists.txt)
    return()
endif()

include(${CMAKE_CURRENT_LIST_DIR}/cmake/pouch_target.cmake)

if(CONFIG_POUCH)
    pouch_target_add_core_sources(pouch ${CMAKE_CURRENT_LIST_DIR})
    pouch_target_add_header_codegen(pouch ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_BINARY_DIR})
    pouch_validate_config()

    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/golioth_sdk golioth_sdk)
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/src/transport transport)

endif()

# Libraries

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/lib lib)
