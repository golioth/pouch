set(GOLIOTH_SDK_ROOT ../..)

list(APPEND GOLIOTH_SDK_SRCS ${GOLIOTH_SDK_ROOT}/dispatch.c)
list(APPEND GOLIOTH_SDK_LINKER_FILES ${GOLIOTH_SDK_ROOT}/dispatch.lf)
list(APPEND POUCH_FORCE_LINK dispatch_c)

if ("${CONFIG_GOLIOTH_SETTINGS}" STREQUAL "y")
    list(APPEND GOLIOTH_SDK_SRCS ${GOLIOTH_SDK_ROOT}/settings.c)
    list(APPEND GOLIOTH_SDK_SRCS ${GOLIOTH_SDK_ROOT}/settings_callbacks.c)
    list(APPEND GOLIOTH_SDK_LINKER_FILES ${GOLIOTH_SDK_ROOT}/settings_callbacks.lf)
    list(APPEND POUCH_FORCE_LINK settings_c)
endif()

if ("${CONFIG_GOLIOTH_OTA}" STREQUAL "y")
    list(APPEND GOLIOTH_SDK_SRCS
        ${GOLIOTH_SDK_ROOT}/hex.c
        ${GOLIOTH_SDK_ROOT}/ota.c
        ${GOLIOTH_SDK_ROOT}/ota_upper.c
    )
    list(APPEND GOLIOTH_SDK_LINKER_FILES ${GOLIOTH_SDK_ROOT}/ota.lf)
    list(APPEND POUCH_FORCE_LINK ota_c)
endif()

idf_component_register(SRCS
                        ${GOLIOTH_SDK_SRCS}

                       INCLUDE_DIRS
                        ${GOLIOTH_SDK}

                       LDFRAGMENTS
                        ${GOLIOTH_SDK_LINKER_FILES}

                       REQUIRES
                         pouch
                       )

# Force linking by searching for `void link_hook_filename_c(void) { }` hooks
foreach(NAME ${POUCH_FORCE_LINK})
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-u link_hook_${NAME}")
endforeach()
