set(GOLIOTH_SDK_ROOT ../..)

list(APPEND GOLIOTH_SDK_SRCS ${GOLIOTH_SDK_ROOT}/dispatch.c)
list(APPEND GOLIOTH_SDK_LINKER_FILES ${GOLIOTH_SDK_ROOT}/dispatch.lf)

if ("${CONFIG_GOLIOTH_SETTINGS}" STREQUAL "y")
    list(APPEND GOLIOTH_SDK_SRCS ${GOLIOTH_SDK_ROOT}/settings.c)
    list(APPEND GOLIOTH_SDK_SRCS ${GOLIOTH_SDK_ROOT}/settings_callbacks.c)
    list(APPEND GOLIOTH_SDK_LINKER_FILES ${GOLIOTH_SDK_ROOT}/settings_callbacks.lf)
endif()

if ("${CONFIG_GOLIOTH_OTA}" STREQUAL "y")
    list(APPEND GOLIOTH_SDK_SRCS
        ${GOLIOTH_SDK_ROOT}/hex.c
        ${GOLIOTH_SDK_ROOT}/ota.c
        ${GOLIOTH_SDK_ROOT}/ota_upper.c
    )
    list(APPEND GOLIOTH_SDK_LINKER_FILES ${GOLIOTH_SDK_ROOT}/ota.lf)
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

# Force linking of files that don't have any global references (eg: dispatch.c, settings.c, ota.c)
# https://github.com/espressif/esp-idf/tree/release/v5.4/examples/build_system/cmake/plugins#ensuring-that-the-plugin-code-is-included-into-the-executable
idf_component_set_property(${COMPONENT_NAME} WHOLE_ARCHIVE TRUE)
