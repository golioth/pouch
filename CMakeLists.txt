# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

# Helpers
include(${CMAKE_CURRENT_LIST_DIR}/cmake/pouch_target.cmake)

# Platform Specific
if(ESP_PLATFORM)
    include(${CMAKE_CURRENT_LIST_DIR}/port/esp_idf/CMakeLists.txt)

    # ESP-IDF evaluates component CMake in script mode for dependency discovery.
    # In that mode, add_subdirectory() is not scriptable, so stop before shared subdirs.
    if(CMAKE_SCRIPT_MODE_FILE)
        return()
    endif()
endif()

if(COMMAND zephyr_library_named)
    include(${CMAKE_CURRENT_LIST_DIR}/port/zephyr/CMakeLists.txt)
endif()

# Core
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/src src)
