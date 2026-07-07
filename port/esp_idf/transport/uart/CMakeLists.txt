# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

pouch_config_enabled(_pouch_uart_device CONFIG_POUCH_UART_DEVICE)
pouch_config_enabled(_pouch_uart_broker CONFIG_POUCH_UART_BROKER)

if(NOT _pouch_uart_device AND NOT _pouch_uart_broker)
    return()
endif()

target_sources(${POUCH_ACTIVE_TARGET} PRIVATE
    uart_core.c
)

if(_pouch_uart_device)
    target_sources(${POUCH_ACTIVE_TARGET} PRIVATE device.c)
endif()

if(_pouch_uart_broker)
    target_sources(${POUCH_ACTIVE_TARGET} PRIVATE broker.c)
endif()
