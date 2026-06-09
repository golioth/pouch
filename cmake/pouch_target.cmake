# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

function(_pouch_config_enabled out_var config_name)
    if(DEFINED ${config_name})
        if(${config_name} OR "${${config_name}}" STREQUAL "y")
            set(${out_var} TRUE PARENT_SCOPE)
            return()
        endif()
    endif()

    set(${out_var} FALSE PARENT_SCOPE)
endfunction()

function(pouch_target_add_core_sources target pouch_root)
    target_sources(${target} PRIVATE
        ${pouch_root}/src/pouch.c
        ${pouch_root}/src/uplink.c
        ${pouch_root}/src/header.c
        ${pouch_root}/src/buf.c
        ${pouch_root}/src/block.c
        ${pouch_root}/src/entry.c
        ${pouch_root}/src/stream.c
        ${pouch_root}/src/downlink.c
    )

    _pouch_config_enabled(_pouch_encryption_none CONFIG_POUCH_ENCRYPTION_NONE)
    if(_pouch_encryption_none)
        target_sources(${target} PRIVATE ${pouch_root}/src/crypto_none.c)
    endif()

    _pouch_config_enabled(_pouch_encryption_saead CONFIG_POUCH_ENCRYPTION_SAEAD)
    if(_pouch_encryption_saead)
        target_sources(${target} PRIVATE
            ${pouch_root}/src/crypto_saead.c
            ${pouch_root}/src/cert.c
            ${pouch_root}/src/saead/session.c
            ${pouch_root}/src/saead/uplink.c
            ${pouch_root}/src/saead/downlink.c
        )
    endif()
endfunction()

function(pouch_target_add_header_codegen target pouch_root generated_dir)
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
            COMMAND zcbor code -c ${pouch_root}/src/header.cddl -t pouch_header -sde
                --include-prefix cddl/ --oc header.c --oh include/cddl/header.h
            BYPRODUCTS
                ${generated_dir}/include/cddl/header_decode.h
                ${generated_dir}/include/cddl/header_encode.h
                ${generated_dir}/include/cddl/header_types.h
            DEPENDS ${pouch_root}/src/header.cddl
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

function(pouch_validate_config)
    _pouch_config_enabled(_pouch_encryption_saead CONFIG_POUCH_ENCRYPTION_SAEAD)
    if(NOT _pouch_encryption_saead)
        return()
    endif()

    _pouch_config_enabled(_pouch_validate_cert CONFIG_POUCH_VALIDATE_SERVER_CERT)
    if(NOT _pouch_validate_cert)
        message(WARNING " \n"
                " ************************************************\n"
                " Pouch server certificate validation is disabled.\n"
                " Do not use this in production.\n"
                " ************************************************")
    endif()

    _pouch_config_enabled(_pouch_psa_p256m CONFIG_MBEDTLS_PSA_P256M_DRIVER_ENABLED)
    if(_pouch_psa_p256m)
        message(FATAL_ERROR " \n"
                " **************************************************\n"
                " CONFIG_MBEDTLS_PSA_256M_DRIVER_ENABLED breaks\n"
                " support for the secp384r1 curve, which is required\n"
                " for Pouch. Disable this option in sdkconfig\n"
                " **************************************************")
    endif()
endfunction()
