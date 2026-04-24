set(ZCBOR_DIR ${CMAKE_CURRENT_LIST_DIR}/zcbor)

idf_component_register(
    INCLUDE_DIRS
        "${ZCBOR_DIR}/include"
    SRCS
        "${ZCBOR_DIR}/src/zcbor_common.c"
        "${ZCBOR_DIR}/src/zcbor_decode.c"
        "${ZCBOR_DIR}/src/zcbor_encode.c"
)

# These mappings adapted from the Zephyr-centric approach:
# https://github.com/zephyrproject-rtos/zephyr/blob/v4.3.0/modules/zcbor/CMakeLists.txt
if(CONFIG_ZCBOR_CANONICAL)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ZCBOR_CANONICAL)
endif(CONFIG_ZCBOR_CANONICAL)

if(CONFIG_ZCBOR_STOP_ON_ERROR)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ZCBOR_STOP_ON_ERROR)
endif(CONFIG_ZCBOR_STOP_ON_ERROR)

if(CONFIG_ZCBOR_BIG_ENDIAN)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ZCBOR_BIG_ENDIAN)
endif(CONFIG_ZCBOR_BIG_ENDIAN)

component_compile_options(
    "-Wno-unused-but-set-variable"
)
