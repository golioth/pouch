# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

function(_pouch_src_config_enabled out_var config_name)
    if(DEFINED ${config_name})
        if(${config_name} OR "${${config_name}}" STREQUAL "y")
            set(${out_var} TRUE PARENT_SCOPE)
            return()
        endif()
    endif()

    set(${out_var} FALSE PARENT_SCOPE)
endfunction()

function(pouch_src_apply target generated_dir)
    set(_pouch_src_root ${CMAKE_CURRENT_FUNCTION_LIST_DIR})

    target_sources(${target} PRIVATE
        ${_pouch_src_root}/pouch.c
        ${_pouch_src_root}/uplink.c
        ${_pouch_src_root}/header.c
        ${_pouch_src_root}/buf.c
        ${_pouch_src_root}/block.c
        ${_pouch_src_root}/entry.c
        ${_pouch_src_root}/stream.c
        ${_pouch_src_root}/downlink.c
    )

    _pouch_src_config_enabled(_pouch_encryption_none CONFIG_POUCH_ENCRYPTION_NONE)
    if(_pouch_encryption_none)
        target_sources(${target} PRIVATE ${_pouch_src_root}/crypto_none.c)
    endif()

    _pouch_src_config_enabled(_pouch_encryption_saead CONFIG_POUCH_ENCRYPTION_SAEAD)
    if(_pouch_encryption_saead)
        target_sources(${target} PRIVATE
            ${_pouch_src_root}/crypto_saead.c
            ${_pouch_src_root}/cert.c
            ${_pouch_src_root}/saead/session.c
            ${_pouch_src_root}/saead/uplink.c
            ${_pouch_src_root}/saead/downlink.c
        )
    endif()

    file(MAKE_DIRECTORY ${generated_dir}/include)

    target_sources(${target} PRIVATE
        ${generated_dir}/header_decode.c
        ${generated_dir}/header_encode.c
    )

    target_include_directories(${target} PRIVATE ${generated_dir}/include)

    if(NOT CMAKE_BUILD_EARLY_EXPANSION)
        add_custom_command(
            OUTPUT
                ${generated_dir}/header_decode.c
                ${generated_dir}/header_encode.c
            COMMAND zcbor code -c ${_pouch_src_root}/header.cddl -t pouch_header -sde
                --include-prefix cddl/ --oc header.c --oh include/cddl/header.h
            BYPRODUCTS
                ${generated_dir}/include/cddl/header_decode.h
                ${generated_dir}/include/cddl/header_encode.h
                ${generated_dir}/include/cddl/header_types.h
            DEPENDS ${_pouch_src_root}/header.cddl
            WORKING_DIRECTORY ${generated_dir})

        string(REPLACE "::" "_" _pouch_codegen_target ${target})
        set(_pouch_codegen_target "${_pouch_codegen_target}_generate_headers")
        add_custom_target(${_pouch_codegen_target}
            DEPENDS
                ${generated_dir}/header_decode.c
                ${generated_dir}/header_encode.c)
        add_dependencies(${target} ${_pouch_codegen_target})
    endif()
endfunction()
