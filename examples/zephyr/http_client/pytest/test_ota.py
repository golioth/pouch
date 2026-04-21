#
# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

import logging
import re

import pytest
from twister_harness.device.device_adapter import DeviceAdapter

pytestmark = pytest.mark.anyio


async def test_ota_sha256(dut: DeviceAdapter, ota_firmware):
    expected_sha256 = ota_firmware

    logging.info("Waiting for device to boot and load credentials")
    dut.readlines_until("Credentials loaded", timeout=60.0)

    logging.info("Waiting for OTA download, expected SHA256=%s", expected_sha256)
    lines = dut.readlines_until(
        regex=r"OTA computed SHA256: [0-9a-f]{64}", timeout=180.0
    )

    actual_sha256 = None
    for line in lines:
        match = re.search(r"OTA computed SHA256: ([0-9a-f]{64})", line)
        if match:
            actual_sha256 = match.group(1)
            break

    assert actual_sha256 is not None, "Device did not log OTA SHA256"

    logging.info("Device SHA256: %s", actual_sha256)
    logging.info("Expected SHA256: %s", expected_sha256)

    assert actual_sha256 == expected_sha256, (
        f"SHA256 mismatch: device={actual_sha256}, expected={expected_sha256}"
    )
