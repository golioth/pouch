#
# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

import logging

import pytest
from twister_harness.device.device_adapter import DeviceAdapter

pytestmark = pytest.mark.anyio


@pytest.fixture(scope="module", autouse=True)
async def setup(project, device, creds):
    logging.info("Delete existing device-level LED setting")

    settings = await device.settings.get_all()
    for setting in settings:
        if "deviceId" in setting and setting["key"] == "LED":
            await device.settings.delete(setting["key"])

    logging.info("Ensure the project-level LED setting exists")
    await project.settings.set("LED", False)

    yield

    logging.info("Delete any existing device-level LED settings (cleanup)")

    settings = await device.settings.get_all()
    for setting in settings:
        if "deviceId" in setting and setting["key"] == "LED":
            await device.settings.delete(setting["key"])


async def test_setting_project(dut: DeviceAdapter):
    dut.readlines_until(regex="Credentials loaded", timeout=60.0)
    dut.readlines_until(regex="Received LED setting: 0", timeout=120.0)


async def test_setting_device(device, dut: DeviceAdapter):
    logging.info("Set device-level setting")
    await device.settings.set("LED", True)

    dut.readlines_until(regex="Received LED setting: 1", timeout=120.0)
