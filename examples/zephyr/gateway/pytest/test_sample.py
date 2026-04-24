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
    dut.readlines_until("Bluetooth initialized")

    dut.readlines_until("Starting downlink")

    dut.readlines_until("Received LED setting: 0")


async def test_setting_device(device, dut: DeviceAdapter):
    logging.info("Set device-level setting")
    await device.settings.set("LED", True)

    dut.readlines_until("Received LED setting: 1")
