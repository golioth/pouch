#
# Copyright (c) 2025 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

import logging

import pytest
from twister_harness.device.device_adapter import DeviceAdapter

pytestmark = pytest.mark.anyio


async def test_setting_project(dut: DeviceAdapter):
    dut.readlines_until(regex="Bluetooth initialized")

    dut.readlines_until(regex="Starting downlink")

    dut.readlines_until(regex="Received LED setting: 0")


async def test_setting_device(gateway, dut: DeviceAdapter):
    logging.info("Set device-level setting on gateway")
    await gateway.settings.set("LED", True)

    dut.readlines_until(regex="Received LED setting: 1")
