# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(gatt_broker_test)

target_sources(app PRIVATE
  src/main.c
  ${ZEPHYR_POUCH_MODULE_DIR}/port/zephyr/transport/gatt/broker/broker.c
)

target_include_directories(app PRIVATE
  ${ZEPHYR_POUCH_MODULE_DIR}/include
  ${ZEPHYR_POUCH_MODULE_DIR}/port/include
  ${ZEPHYR_POUCH_MODULE_DIR}/src
  ${ZEPHYR_POUCH_MODULE_DIR}/port/zephyr/transport/gatt/broker
)

target_compile_definitions(app PRIVATE
  CONFIG_BT_MAX_CONN=2
  CONFIG_POUCH_GATT_WINDOW_SIZE=4
  CONFIG_POUCH_GATEWAY_GATT_LOG_LEVEL=3
)
