#
# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

import logging
import re

import pytest
from twister_harness.device.device_adapter import DeviceAdapter

logger = logging.getLogger(__name__)

pytestmark = pytest.mark.anyio

OTA_DEFAULT_TIMEOUT_S = 60.0
OTA_DOWNLINK_TIMEOUT_S = 120.0
OTA_DOWNLOAD_TIMEOUT_S = 180.0
OTA_REBOOT_TIMEOUT_S = 180.0


@pytest.mark.ota_mode("dummy")
async def test_ota_sha256(dut: DeviceAdapter, ota_update):
    expected_sha256 = ota_update

    logger.info("Waiting for OTA download, expected SHA256=%s", expected_sha256)
    lines = dut.readlines_until(
        regex=r"OTA computed SHA256: [0-9a-f]{64}", timeout=OTA_DOWNLOAD_TIMEOUT_S
    )

    actual_sha256 = None
    for line in lines:
        match = re.search(r"OTA computed SHA256: ([0-9a-f]{64})", line)
        if match:
            actual_sha256 = match.group(1)
            break

    assert actual_sha256 is not None, "Device did not log OTA SHA256"

    logger.info("Device SHA256: %s", actual_sha256)
    logger.info("Expected SHA256: %s", expected_sha256)

    assert actual_sha256 == expected_sha256, (
        f"SHA256 mismatch: device={actual_sha256}, expected={expected_sha256}"
    )


@pytest.mark.ota_mode("firmware")
async def test_ota_firmware_update(
    dut: DeviceAdapter,
    ota_update,
    fw_update_ver,
):
    dut.readlines_until(
        regex=".*Receiving Downlink entry on path.*", timeout=OTA_DOWNLINK_TIMEOUT_S
    )

    dut.readlines_until(
        regex=".*fw_update: Rebooting to apply upgrade",
        timeout=OTA_DOWNLOAD_TIMEOUT_S,
    )

    dut.readlines_until(
        regex=rf".*Image version: v{re.escape(fw_update_ver)}",
        timeout=OTA_REBOOT_TIMEOUT_S,
    )

    dut.readlines_until(
        regex=".*Credentials loaded.*",
        timeout=OTA_DEFAULT_TIMEOUT_S,
    )

    dut.readlines_until(
        regex=r".*Received LED setting: [0-1]",
        timeout=OTA_DEFAULT_TIMEOUT_S,
    )
