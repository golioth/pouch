# PDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(downlink_test)

target_sources(app PRIVATE
  src/downlink.c
)

set(gen_dir ${ZEPHYR_BINARY_DIR}/include/generated/)

function(pouch_gen_lorem length)
  add_custom_command(
    OUTPUT ${gen_dir}/lorem-${length}.bin
    COMMAND
    ${PYTHON_EXECUTABLE}
    ${APPLICATION_SOURCE_DIR}/scripts/pouch-gen-lorem.py
    --length ${length}
    ${gen_dir}/lorem-${length}.bin
    DEPENDS ${APPLICATION_SOURCE_DIR}/scripts/pouch-gen-lorem.py
    WORKING_DIRECTORY ${APPLICATION_SOURCE_DIR}/
  )
  add_custom_target(pouch-gen-lorem-${length} DEPENDS ${gen_dir}/lorem-${length}.bin)
  add_dependencies(app pouch-gen-lorem-${length})
  generate_inc_file_for_target(app ${gen_dir}/lorem-${length}.bin ${gen_dir}/lorem-${length}.inc)
endfunction()

pouch_gen_lorem(10)
pouch_gen_lorem(512)
pouch_gen_lorem(1024)
pouch_gen_lorem(102400)
