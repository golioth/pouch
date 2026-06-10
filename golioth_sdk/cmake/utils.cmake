function(golioth_sdk_config_enabled out_var config_name)
    if(DEFINED ${config_name})
        if(${config_name} OR "${${config_name}}" STREQUAL "y")
            set(${out_var} TRUE PARENT_SCOPE)
        else()
            set(${out_var} FALSE PARENT_SCOPE)
        endif()
        return()
    endif()

    if(ESP_PLATFORM AND CMAKE_BUILD_EARLY_EXPANSION)
        # During ESP-IDF early expansion, component CMake files are evaluated in
        # script mode before sdkconfig-derived CONFIG_* values are guaranteed to
        # exist. Default undefined symbols to enabled in that phase so component
        # sources/dependencies are not dropped from dependency enumeration.
        # Outside early expansion, undefined symbols should be treated as off.
        set(${out_var} TRUE PARENT_SCOPE)
    else()
        set(${out_var} FALSE PARENT_SCOPE)
    endif()
endfunction()

function(golioth_sdk_require_defined_vars)
    foreach(_var IN LISTS ARGN)
        if(NOT DEFINED ${_var})
            message(FATAL_ERROR "golioth_sdk: required variable '${_var}' is not defined")
        endif()
    endforeach()
endfunction()
