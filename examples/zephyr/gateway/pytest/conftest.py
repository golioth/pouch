#
# Copyright (c) 2025 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

import logging
import os
import random
import string
import subprocess
import sys
from pathlib import Path
from typing import Generator

sys.path.insert(
    0, str(Path(__file__).resolve().parents[4] / "scripts" / "pytest-pouch")
)

pytest_plugins = ["pytest_pouch.plugin"]

import pytest  # noqa: E402

from twister_harness.device.device_adapter import DeviceAdapter  # noqa: E402
from twister_harness.twister_harness_config import TwisterHarnessConfig  # noqa: E402


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
def creds_dir(twister_harness_config: TwisterHarnessConfig):
    """Override default creds_dir for gateway sysbuild layout."""
    return (
        twister_harness_config.devices[0].build_dir
        / "peripheral_ble_gatt_example_0"
        / "creds"
    )


@pytest.fixture(scope="module")
def gateway_creds_dir(twister_harness_config: TwisterHarnessConfig):
    """Credential directory for the gateway application (DTLS certs).

    Supports both the 'gateway' and 'gateway_custom_connect' image names.
    """
    build_dir = twister_harness_config.devices[0].build_dir
    for name in ["gateway", "gateway_custom_connect"]:
        if (build_dir / name).exists():
            return build_dir / name / "creds"
    # Default fallback
    return build_dir / "gateway" / "creds"


@pytest.fixture(scope="module")
async def gateway_creds(gateway_creds_dir, gateway, creds_dir, creds, project):
    """Generate gateway DTLS credentials using the same CA as the peripheral.

    This fixture depends on `creds` so the CA key/cert are already
    generated in `creds_dir` before we run.
    """
    gateway_creds_dir.mkdir(mode=0o755, exist_ok=True, parents=True)

    ca_key = creds_dir / "ca.key.pem"
    ca_cert = creds_dir / "ca.crt.pem"

    logging.info("Generate gateway device private key and cert (signed by shared CA)")

    subprocess.run(
        f"openssl ecparam -name prime256v1 -genkey -noout -out {gateway.name}.key.pem",
        check=True,
        shell=True,
        cwd=gateway_creds_dir,
    )
    subprocess.run(
        f"""\
    openssl req -new \
        -key {gateway.name}.key.pem \
        -subj "/C=US/O={project.id}/CN={gateway.name}" \
        -out {gateway.name}.csr.pem""",
        check=True,
        shell=True,
        cwd=gateway_creds_dir,
    )
    subprocess.run(
        f"""\
    openssl x509 -req \
        -in "{gateway.name}.csr.pem" \
        -CA "{ca_cert}" \
        -CAkey "{ca_key}" \
        -CAcreateserial \
        -out "{gateway.name}.crt.pem" \
        -days 500 -sha256""",
        check=True,
        shell=True,
        cwd=gateway_creds_dir,
    )

    logging.info("Convert gateway key and cert to DER format")

    subprocess.run(
        f"openssl x509 -in {gateway.name}.crt.pem -outform DER -out crt.der",
        check=True,
        shell=True,
        cwd=gateway_creds_dir,
    )
    subprocess.run(
        f"openssl ec -in {gateway.name}.key.pem -outform DER -out key.der",
        check=True,
        shell=True,
        cwd=gateway_creds_dir,
    )

    logging.info("Convert CA cert to DER for gateway DTLS")

    subprocess.run(
        f"openssl x509 -in {ca_cert} -outform DER -out ca.der",
        check=True,
        shell=True,
        cwd=gateway_creds_dir,
    )


@pytest.fixture(scope="module", autouse=True)
async def setup(project, device, gateway, creds):
    logging.info("Delete existing device-level LED setting")

    settings = await device.settings.get_all()
    for setting in settings:
        if "deviceId" in setting and setting["key"] == "LED":
            await device.settings.delete(setting["key"])

    gw_settings = await gateway.settings.get_all()
    for setting in gw_settings:
        if "deviceId" in setting and setting["key"] == "LED":
            await gateway.settings.delete(setting["key"])

    logging.info("Ensure the project-level LED setting exists")
    await project.settings.set("LED", False)

    yield

    logging.info("Delete any existing device-level LED settings (cleanup)")

    settings = await device.settings.get_all()
    for setting in settings:
        if "deviceId" in setting and setting["key"] == "LED":
            await device.settings.delete(setting["key"])

    gw_settings = await gateway.settings.get_all()
    for setting in gw_settings:
        if "deviceId" in setting and setting["key"] == "LED":
            await gateway.settings.delete(setting["key"])
