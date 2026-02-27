#
# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(serial_modem)

# NORDIC SDK APP START
target_sources(app PRIVATE ${ZEPHYR_NCS_SERIAL_MODEM_MODULE_DIR}/app/src/main.c)
target_sources(app PRIVATE ${ZEPHYR_NCS_SERIAL_MODEM_MODULE_DIR}/app/src/sm_util.c)
target_sources(app PRIVATE ${ZEPHYR_NCS_SERIAL_MODEM_MODULE_DIR}/app/src/sm_ctrl_pin.c)
target_sources(app PRIVATE ${ZEPHYR_NCS_SERIAL_MODEM_MODULE_DIR}/app/src/sm_settings.c)
target_sources(app PRIVATE ${ZEPHYR_NCS_SERIAL_MODEM_MODULE_DIR}/app/src/sm_at_host.c)
target_sources(app PRIVATE ${ZEPHYR_NCS_SERIAL_MODEM_MODULE_DIR}/app/src/sm_at_commands.c)
target_sources(app PRIVATE ${ZEPHYR_NCS_SERIAL_MODEM_MODULE_DIR}/app/src/sm_at_socket.c)
target_sources(app PRIVATE ${ZEPHYR_NCS_SERIAL_MODEM_MODULE_DIR}/app/src/sm_at_icmp.c)
target_sources(app PRIVATE ${ZEPHYR_NCS_SERIAL_MODEM_MODULE_DIR}/app/src/sm_at_fota.c)
target_sources(app PRIVATE ${ZEPHYR_NCS_SERIAL_MODEM_MODULE_DIR}/app/src/sm_at_dfu.c)
target_sources(app PRIVATE ${ZEPHYR_NCS_SERIAL_MODEM_MODULE_DIR}/app/src/sm_uart_handler.c)
# NORDIC SDK APP END
target_sources_ifdef(CONFIG_SM_SMS app PRIVATE ${ZEPHYR_NCS_SERIAL_MODEM_MODULE_DIR}/app/src/sm_at_sms.c)
target_sources_ifdef(CONFIG_SM_PPP app PRIVATE ${ZEPHYR_NCS_SERIAL_MODEM_MODULE_DIR}/app/src/sm_ppp.c)
target_sources_ifdef(CONFIG_SM_CMUX app PRIVATE ${ZEPHYR_NCS_SERIAL_MODEM_MODULE_DIR}/app/src/sm_cmux.c)
target_sources_ifdef(CONFIG_SM_GNSS app PRIVATE ${ZEPHYR_NCS_SERIAL_MODEM_MODULE_DIR}/app/src/sm_at_gnss.c)
target_sources_ifdef(CONFIG_SM_NRF_CLOUD app PRIVATE ${ZEPHYR_NCS_SERIAL_MODEM_MODULE_DIR}/app/src/sm_at_nrfcloud.c)
target_sources_ifdef(CONFIG_SM_MQTTC app PRIVATE ${ZEPHYR_NCS_SERIAL_MODEM_MODULE_DIR}/app/src/sm_at_mqtt.c)
target_sources_ifdef(CONFIG_SM_MODEM_TRACE_BACKEND_CMUX app PRIVATE ${ZEPHYR_NCS_SERIAL_MODEM_MODULE_DIR}/app/src/sm_trace_backend_cmux.c)

add_subdirectory_ifdef(CONFIG_SM_CARRIER ${ZEPHYR_NCS_SERIAL_MODEM_MODULE_DIR}/app/src/lwm2m_carrier)

zephyr_include_directories(${ZEPHYR_NCS_SERIAL_MODEM_MODULE_DIR}/app/src)

include(${ZEPHYR_NCS_SERIAL_MODEM_MODULE_DIR}/app/cmake/sm_version.cmake)
