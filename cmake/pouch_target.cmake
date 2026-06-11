# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

function(pouch_config_enabled out_var config_name)
    if(DEFINED ${config_name})
        if(${config_name} OR "${${config_name}}" STREQUAL "y")
            set(${out_var} TRUE PARENT_SCOPE)
            return()
        endif()
    endif()

    set(${out_var} FALSE PARENT_SCOPE)
endfunction()

function(pouch_validate_config)
    pouch_config_enabled(_pouch_encryption_saead CONFIG_POUCH_ENCRYPTION_SAEAD)
    if(NOT _pouch_encryption_saead)
        return()
    endif()

    pouch_config_enabled(_pouch_validate_cert CONFIG_POUCH_VALIDATE_SERVER_CERT)
    if(NOT _pouch_validate_cert)
        message(WARNING " \n"
                " ************************************************\n"
                " Pouch server certificate validation is disabled.\n"
                " Do not use this in production.\n"
                " ************************************************")
    endif()

    pouch_config_enabled(_pouch_psa_p256m CONFIG_MBEDTLS_PSA_P256M_DRIVER_ENABLED)
    if(_pouch_psa_p256m)
        message(FATAL_ERROR " \n"
                " **************************************************\n"
                " CONFIG_MBEDTLS_PSA_P256M_DRIVER_ENABLED breaks\n"
                " support for the secp384r1 curve, which is required\n"
                " for Pouch. Disable this option in sdkconfig\n"
                " **************************************************")
    endif()
endfunction()
