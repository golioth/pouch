# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(gateway)

target_include_directories(app PRIVATE include ${CMAKE_CURRENT_BINARY_DIR})

target_sources(app PRIVATE
  src/credentials.c
  src/fatal_error.c
  src/main.c
  git_describe.h
)

if(CONFIG_WIFI)
  target_sources(app PRIVATE src/wifi.c)
endif()

file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/mk-git-describe.sh
  "echo \"#define GIT_DESCRIBE \\\\\" > ${CMAKE_CURRENT_BINARY_DIR}/git_describe.h\n"
  "git describe --always --dirty >> ${CMAKE_CURRENT_BINARY_DIR}/git_describe.h || true\n")

add_custom_command(OUTPUT git_describe.h always_run
  COMMAND chmod +x ${CMAKE_CURRENT_BINARY_DIR}/mk-git-describe.sh
  COMMAND ${CMAKE_CURRENT_BINARY_DIR}/mk-git-describe.sh
  WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR})

if (NOT CONFIG_EXAMPLE_COAP_CLIENT_DTLS_LOAD_CA_FROM_FILESYSTEM)
  generate_inc_file_for_target(app
    ${CMAKE_CURRENT_SOURCE_DIR}/../http_client/src/isrgrootx1.der
    ${ZEPHYR_BINARY_DIR}/include/generated/pouch-ca-crt.inc)
endif()
