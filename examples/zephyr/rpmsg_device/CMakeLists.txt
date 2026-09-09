# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(rpmsg_device)

target_sources(app PRIVATE
  src/main.c
  src/credentials.c
)

if(CONFIG_EXAMPLE_FW_RELAY OR CONFIG_EXAMPLE_FW_SIGNED_URL)
  target_sources(app PRIVATE src/fw_update.c)
endif()

target_sources_ifdef(CONFIG_EXAMPLE_FW_RELAY app PRIVATE src/fw_relay.c)
target_sources_ifdef(CONFIG_EXAMPLE_FW_SIGNED_URL app PRIVATE src/fw_signed_url.c)

# Signing needs a wall clock, so signy selects POSIX_TIMERS, which in Zephyr
# 4.4 requires POSIX_SYSTEM_INTERFACES, which puts include/zephyr/posix on the
# global include path. mbedTLS then finds <arpa/inet.h> through __has_include()
# while looking for inet_pton(), and that header declares its functions with
# sa_family_t - a type only defined when the networking stack is built. A device
# that speaks Pouch over rpmsg has no IP stack, so x509_crt.c fails to compile.
#
# Aliasing the type to the one the networking headers actually define is enough:
# arpa/inet.h pulls in zephyr/net/net_ip.h, where net_sa_family_t is defined
# unconditionally. Nothing here calls inet_pton(); the declaration only has to
# parse. Drop this once Zephyr's arpa/inet.h stops depending on a type it does
# not define, or once signy stops requiring Zephyr's POSIX clock.
#
# Keyed on the headers being present rather than on this sample using signy:
# enabling CONFIG_SIGNY is what puts them there, and that can outlive any one
# of the options above - a build with the transport support compiled but the
# sample's signed-URL path switched off still needs this.
if(CONFIG_POSIX_SYSTEM_INTERFACES AND NOT CONFIG_NETWORKING)
  zephyr_compile_definitions(sa_family_t=net_sa_family_t)
endif()
