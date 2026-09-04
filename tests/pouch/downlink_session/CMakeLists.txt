# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(downlink_session_test)

# This suite exercises src/saead/downlink.c and src/saead/session.c with
# real PSA crypto (ECDH + AEAD), so we can't use CONFIG_POUCH_ENCRYPTION_MOCK.
# Rather than pulling in the whole pouch library (which would need a real
# device/server X.509 certificate chain via src/cert.c just to reach the
# session code), we compile only the sources under test plus test doubles
# for their few dependencies outside the saead module.
target_sources(app PRIVATE
  src/downlink_session.c
  ${ZEPHYR_POUCH_MODULE_DIR}/src/saead/downlink.c
  ${ZEPHYR_POUCH_MODULE_DIR}/src/saead/session.c
  ${ZEPHYR_POUCH_MODULE_DIR}/src/buf.c
)

target_include_directories(app PRIVATE
  ${ZEPHYR_POUCH_MODULE_DIR}/include
  ${ZEPHYR_POUCH_MODULE_DIR}/port/include
  ${ZEPHYR_POUCH_MODULE_DIR}/src
  ${ZEPHYR_BINARY_DIR}/include/generated/include
)

# Provide the macros that the bypassed POUCH Kconfig tree would normally
# generate (see src/Kconfig and the module log config).
target_compile_definitions(app PRIVATE
  CONFIG_POUCH_COMMON_LOG_LEVEL=3
  CONFIG_POUCH_AUTH_TAG_LEN=16
  CONFIG_POUCH_BLOCK_SIZE=512
)

# src/saead/uplink.h (pulled in transitively by src/saead/downlink.c) needs
# the zcbor-generated cddl/header_types.h for struct saead_info. We don't
# need the generated codec itself (header_decode.c/header_encode.c) since
# we never touch the pouch header layer here, just the type definitions.
set(gen_dir ${ZEPHYR_BINARY_DIR}/include/generated)
file(MAKE_DIRECTORY ${gen_dir})

add_custom_command(
  OUTPUT
    ${gen_dir}/header_decode.c
    ${gen_dir}/header_encode.c
    ${gen_dir}/include/cddl/header_types.h
  COMMAND
    zcbor code -c ${ZEPHYR_POUCH_MODULE_DIR}/src/header.cddl -t pouch_header -sde
      --include-prefix cddl/ --oc header.c --oh include/cddl/header.h
  DEPENDS ${ZEPHYR_POUCH_MODULE_DIR}/src/header.cddl
  WORKING_DIRECTORY ${gen_dir})

add_custom_target(downlink_session_generate_headers
  DEPENDS ${gen_dir}/include/cddl/header_types.h)
add_dependencies(app downlink_session_generate_headers)
