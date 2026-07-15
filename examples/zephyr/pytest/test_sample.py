#
# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

import logging

import pytest
from twister_harness.device.device_adapter import DeviceAdapter

pytestmark = pytest.mark.anyio


async def test_setting_project(dut: DeviceAdapter):
    dut.readlines_until(regex="Received LED setting: 0", timeout=120.0)


async def test_setting_device(settings_device, dut: DeviceAdapter):
    logging.info("Set device-level setting")
    await settings_device.settings.set("LED", True)

    dut.readlines_until(regex="Received LED setting: 1", timeout=120.0)
