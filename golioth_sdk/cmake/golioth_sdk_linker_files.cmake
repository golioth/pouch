get_filename_component(GOLIOTH_SDK_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

function(golioth_sdk_get_linker_files_no_ext out_var)
    set(linker_files "")
    list(APPEND linker_files ${GOLIOTH_SDK_ROOT}/dispatch)
    list(APPEND linker_files ${GOLIOTH_SDK_ROOT}/ota)
    list(APPEND linker_files ${GOLIOTH_SDK_ROOT}/settings_callbacks)
    set(${out_var} "${linker_files}" PARENT_SCOPE)
endfunction()
