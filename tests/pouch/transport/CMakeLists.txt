# PDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(transport_test)

target_sources(app PRIVATE
  src/sender.c
  src/receiver.c
  ${ZEPHYR_POUCH_MODULE_DIR}/src/transport/sar/sender.c
  ${ZEPHYR_POUCH_MODULE_DIR}/src/transport/sar/receiver.c
  ${ZEPHYR_POUCH_MODULE_DIR}/src/transport/sar/packet.c
)
target_include_directories(app PRIVATE
    ${ZEPHYR_POUCH_MODULE_DIR}/src
)
