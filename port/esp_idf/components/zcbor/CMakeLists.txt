set(zcbor_dir ${CMAKE_CURRENT_LIST_DIR}/zcbor)

message(WARNING "ZCBOR: ${zcbor_dir}/include")

idf_component_register(
    INCLUDE_DIRS
        "${zcbor_dir}/include"
    SRCS
        "${zcbor_dir}/src/zcbor_common.c"
        "${zcbor_dir}/src/zcbor_decode.c"
        "${zcbor_dir}/src/zcbor_encode.c"
)

component_compile_options("-Wno-strict-aliasing" "-Wno-uninitialized")
