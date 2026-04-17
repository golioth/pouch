#
# Copyright (c) 2025 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

import logging
import os
import random
import string
import sys
from pathlib import Path
from typing import Generator

sys.path.insert(
    0, str(Path(__file__).resolve().parents[3] / "scripts" / "pytest-pouch")
)

pytest_plugins = ["pytest_pouch.plugin"]

import pytest  # noqa: E402

from twister_harness.device.device_adapter import DeviceAdapter  # noqa: E402


def pytest_addoption(parser):
    parser.addoption("--gateway-name", type=str, help="Golioth gateway name")


@pytest.fixture(scope="session")
def gateway_name(request):
    if request.config.getoption("--gateway-name") is not None:
        return request.config.getoption("--gateway-name")
    elif "GOLIOTH_GATEWAY_NAME" in os.environ:
        return os.environ["GOLIOTH_GATEWAY_NAME"]
    else:
        return None


@pytest.fixture(scope="module")
async def gateway(request, project, gateway_name):
    if gateway_name is not None:
        gateway = await project.device_by_name(gateway_name)
        yield gateway
    else:
        name = "generated-gw-" + "".join(
            random.choice(string.ascii_uppercase + string.ascii_lowercase)
            for i in range(16)
        )
        if request.config.getoption("--mask-secrets"):
            print(f"::add-mask::{name}")
        gateway = await project.create_device(name, name)
        await gateway.credentials.add(name, name)

        yield gateway

        await project.delete_device(gateway)


def determine_scope(fixture_name, config):
    if dut_scope := config.getoption("--dut-scope", None):
        return dut_scope
    return "function"


@pytest.fixture(scope=determine_scope)
async def dut(
    gateway, request: pytest.FixtureRequest, device_object: DeviceAdapter
) -> Generator[DeviceAdapter, None, None]:
    """Return launched device - with run application."""
    device_object.initialize_log_files(request.node.name)
    try:
        # Override direct 'zephyr.exe' execution with invocation of 'west flash -d application_dir',
        # which supports executing launching all domains (BabbleSim components).
        device_object.command = [
            device_object.west,
            "flash",
            "-d",
            str(device_object.device_config.build_dir),
        ]
        device_object.process_kwargs["cwd"] = str(device_object.device_config.build_dir)

        gateway_cred = (await gateway.credentials.list())[0]
        device_object.process_kwargs["env"]["GOLIOTH_SAMPLE_PSK_ID"] = (
            gateway_cred.identity
        )
        device_object.process_kwargs["env"]["GOLIOTH_SAMPLE_PSK"] = gateway_cred.key
        device_object.launch()
        yield device_object
    finally:  # to make sure we close all running processes execution
        device_object.close()


@pytest.fixture(scope="module")
def creds_dir(request: pytest.FixtureRequest):
    """Override default creds_dir for gateway sysbuild layout."""
    return (
        Path(request.config.option.build_dir)
        / "peripheral_ble_gatt_example_0"
        / "creds"
    )


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
