# PDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(downlink_test)

target_sources(app PRIVATE
  src/downlink.c
)

set(gen_dir ${ZEPHYR_BINARY_DIR}/include/generated/)

function(pouch_gen_lorem length count)
  add_custom_command(
    OUTPUT ${gen_dir}/lorem-${length}-x${count}.c
    COMMAND
    ${PYTHON_EXECUTABLE}
    ${APPLICATION_SOURCE_DIR}/scripts/pouch-gen-lorem.py
    --device-name ${CONFIG_POUCH_DEVICE_NAME}
    --length ${length}
    --count ${count}
    lorem_${length}_x${count}
    ${gen_dir}/lorem-${length}-x${count}.c
    DEPENDS ${APPLICATION_SOURCE_DIR}/scripts/pouch-gen-lorem.py
    WORKING_DIRECTORY ${APPLICATION_SOURCE_DIR}/
  )
  add_custom_target(pouch-gen-lorem-${length}-x${count} DEPENDS ${gen_dir}/lorem-${length}-x${count}.c)
  add_dependencies(app pouch-gen-lorem-${length}-x${count})
endfunction()

pouch_gen_lorem(10 1)
pouch_gen_lorem(512 1)
pouch_gen_lorem(1024 1)
pouch_gen_lorem(102400 1)

pouch_gen_lorem(10 5)
pouch_gen_lorem(100 5)
pouch_gen_lorem(200 5)
pouch_gen_lorem(500 5)
pouch_gen_lorem(1024 5)
