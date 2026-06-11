# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(gateway_test)

# The gateway library's full Zephyr build pulls in the BT broker
# transport which requires a Bluetooth stack.  For unit tests we
# compile only the gateway core sources we want to exercise, alongside
# the test sources.
target_sources(app PRIVATE
  src/cloud.c
  src/downlink.c
  src/uplink.c
  src/stub_blockbuf.c
  ${ZEPHYR_POUCH_MODULE_DIR}/src/gateway/cloud.c
  ${ZEPHYR_POUCH_MODULE_DIR}/src/gateway/downlink.c
  ${ZEPHYR_POUCH_MODULE_DIR}/src/gateway/uplink.c
  ${ZEPHYR_POUCH_MODULE_DIR}/src/buf.c
)

target_include_directories(app PRIVATE
  ${ZEPHYR_POUCH_MODULE_DIR}/include
  ${ZEPHYR_POUCH_MODULE_DIR}/port/include
  ${ZEPHYR_POUCH_MODULE_DIR}/src
  ${ZEPHYR_POUCH_MODULE_DIR}/src/gateway
)

# Provide the macros that the bypassed POUCH_GATEWAY Kconfig tree would
# normally generate.
target_compile_definitions(app PRIVATE
  CONFIG_POUCH_GATEWAY_LOG_LEVEL=3
  CONFIG_POUCH_GATEWAY_NUM_BLOCKS=4
  CONFIG_POUCH_GATEWAY_DOWNLINK_BLOCK_TIMEOUT=1
  CONFIG_POUCH_GATEWAY_CLOUD=1
  CONFIG_POUCH_BLOCK_SIZE=1024
)
