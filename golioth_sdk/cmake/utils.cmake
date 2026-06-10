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

function(golioth_sdk_linker_file_for_platform out_var base_path_no_ext)
    if(ESP_PLATFORM)
        set(${out_var} "${base_path_no_ext}.lf" PARENT_SCOPE)
    elseif(COMMAND zephyr_library_named)
        set(${out_var} "${base_path_no_ext}.ld" PARENT_SCOPE)
    else()
        set(${out_var} "" PARENT_SCOPE)
    endif()
endfunction()

function(register_linker_file base_path_no_ext)
    golioth_sdk_linker_file_for_platform(_resolved_linker_file ${base_path_no_ext})
    if(NOT _resolved_linker_file)
        return()
    endif()

    if(NOT EXISTS ${_resolved_linker_file})
        message(FATAL_ERROR "Missing linker file: ${_resolved_linker_file}")
    endif()

    get_property(_linker_files GLOBAL PROPERTY GOLIOTH_SDK_REGISTERED_LINKER_FILES)
    if(NOT _linker_files)
        set(_linker_files)
    endif()

    list(APPEND _linker_files ${_resolved_linker_file})
    set_property(GLOBAL PROPERTY GOLIOTH_SDK_REGISTERED_LINKER_FILES "${_linker_files}")
endfunction()

function(reset_registered_linker_files)
    set_property(GLOBAL PROPERTY GOLIOTH_SDK_REGISTERED_LINKER_FILES "")
endfunction()

function(get_registered_linker_files out_var)
    get_property(_linker_files GLOBAL PROPERTY GOLIOTH_SDK_REGISTERED_LINKER_FILES)
    if(NOT _linker_files)
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()

    set(${out_var} ${_linker_files} PARENT_SCOPE)
endfunction()

function(golioth_sdk_require_defined_vars)
    foreach(_var IN LISTS ARGN)
        if(NOT DEFINED ${_var})
            message(FATAL_ERROR "golioth_sdk: required variable '${_var}' is not defined")
        endif()
    endforeach()
endfunction()
