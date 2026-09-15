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
from collections.abc import Generator
from pathlib import Path

sys.path.insert(
    0, str(Path(__file__).resolve().parents[4] / "scripts" / "pytest-pouch")
)
sys.path.insert(0, str(Path(os.environ["ZEPHYR_BASE"]) / "scripts" / "west_commands"))

pytest_plugins = ["pytest_pouch.plugin"]

import anyio
import pytest
from pytest_pouch.plugin import generate_device_credentials
from runners.core import BuildConfiguration
from twister_harness.device.device_adapter import DeviceAdapter
from twister_harness.twister_harness_config import TwisterHarnessConfig

logger = logging.getLogger(__name__)


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

        yield gateway

        await project.delete_device(gateway)


def determine_scope(fixture_name, config):
    if dut_scope := config.getoption("--dut-scope", None):
        return dut_scope
    return "function"


@pytest.fixture(scope=determine_scope)
async def dut(
    gateway, gateway_creds, request: pytest.FixtureRequest, device_object: DeviceAdapter
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

        device_object.launch()
        yield device_object
    finally:  # to make sure we close all running processes execution
        device_object.close()


@pytest.fixture(scope="module")
def peripheral_build_conf(twister_harness_config: TwisterHarnessConfig):
    """Resolved configuration of the Pouch peripheral image."""
    return BuildConfiguration(
        str(
            twister_harness_config.devices[0].build_dir
            / "peripheral_ble_gatt_example_0"
        )
    )


@pytest.fixture(scope="module")
def gateway_build_dir(twister_harness_config: TwisterHarnessConfig):
    """Build directory for either gateway application."""
    build_dir = twister_harness_config.devices[0].build_dir
    for name in ["gateway", "gateway_custom_connect"]:
        if (build_dir / name).exists():
            return build_dir / name
    # Default fallback
    return build_dir / "gateway"


@pytest.fixture(scope="module")
def gateway_build_conf(gateway_build_dir):
    return BuildConfiguration(str(gateway_build_dir))


@pytest.fixture(scope="module")
async def creds(ca, project):
    """Register the shared CA independently of optional peripheral images."""
    cert_pem = await anyio.Path(ca.cert).read_bytes()
    root_cert = await project.certificates.add(cert_pem, "root")
    yield root_cert["data"]["id"]
    await project.certificates.delete(root_cert["data"]["id"])


@pytest.fixture(scope="module")
async def peripheral_creds(
    request, twister_harness_config, creds_dir, ca, device, project
):
    """Write the BLE identity only when the peripheral image is present."""
    build_dir = twister_harness_config.devices[0].build_dir
    if not (build_dir / "peripheral_ble_gatt_example_0").exists():
        return

    peripheral_build_conf = request.getfixturevalue("peripheral_build_conf")
    await generate_device_credentials(
        creds_dir,
        device.name,
        project.id,
        ca.key,
        ca.cert,
        crt_der_name=peripheral_build_conf.get(
            "CONFIG_EXAMPLE_POUCH_DEVICE_CRT_FILENAME", "crt.der"
        ),
        key_der_name=peripheral_build_conf.get(
            "CONFIG_EXAMPLE_POUCH_DEVICE_KEY_FILENAME", "key.der"
        ),
    )


@pytest.fixture(scope="module")
async def gateway_creds(gateway, ca, creds_dir, project, gateway_build_conf):
    """Write the gateway DTLS identity alongside the peripheral and shared CA."""
    await generate_device_credentials(
        creds_dir,
        gateway.name,
        project.id,
        ca.key,
        ca.cert,
        crt_der_name=gateway_build_conf.get(
            "CONFIG_EXAMPLE_COAP_CLIENT_GW_DEVICE_CRT_FILENAME", "crt.der"
        ),
        key_der_name=gateway_build_conf.get(
            "CONFIG_EXAMPLE_COAP_CLIENT_GW_DEVICE_KEY_FILENAME", "key.der"
        ),
    )


@pytest.fixture(scope="module", autouse=True)
async def setup(project, device, gateway, creds, peripheral_creds):
    logger.info("Delete existing device-level LED setting")

    settings = await device.settings.get_all()
    for setting in settings:
        if "deviceId" in setting and setting["key"] == "LED":
            await device.settings.delete(setting["key"])

    gw_settings = await gateway.settings.get_all()
    for setting in gw_settings:
        if "deviceId" in setting and setting["key"] == "LED":
            await gateway.settings.delete(setting["key"])

    logger.info("Ensure the project-level LED setting exists")
    await project.settings.set("LED", False)

    yield

    logger.info("Delete any existing device-level LED settings (cleanup)")

    settings = await device.settings.get_all()
    for setting in settings:
        if "deviceId" in setting and setting["key"] == "LED":
            await device.settings.delete(setting["key"])

    gw_settings = await gateway.settings.get_all()
    for setting in gw_settings:
        if "deviceId" in setting and setting["key"] == "LED":
            await gateway.settings.delete(setting["key"])
