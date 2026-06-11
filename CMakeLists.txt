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
elseif(Zephyr_DIR)
    include(${CMAKE_CURRENT_LIST_DIR}/port/zephyr/CMakeLists.txt)
endif()

# Only add additional sources if CONFIG_POUCH is selected
pouch_config_enabled(_pouch_selected CONFIG_POUCH)
if(NOT _pouch_selected)
    return()
endif()

# Validate Kconfig settings
pouch_validate_config()

# Assert that the Port set required values
if(NOT DEFINED POUCH_ACTIVE_TARGET)
    message(FATAL_ERROR "POUCH_ACTIVE_TARGET must be set by port/<platform>/CMakeLists.txt")
endif()

if(NOT DEFINED POUCH_ACTIVE_GENERATED_DIR)
    message(FATAL_ERROR "POUCH_ACTIVE_GENERATED_DIR must be set by port/<platform>/CMakeLists.txt")
endif()

# Core
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/src src)

# SDKs
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/golioth_sdk golioth_sdk)
