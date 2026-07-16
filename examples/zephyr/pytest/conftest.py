#
# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

import hashlib
import logging
import os
import random
import secrets
import string
import sys
from pathlib import Path
from typing import Generator

import west.configuration

sys.path.insert(
    0, str(Path(__file__).resolve().parents[3] / "scripts" / "pytest-pouch")
)

WEST_TOPDIR = Path(west.configuration.west_dir()).parent
sys.path.insert(0, str(WEST_TOPDIR / "zephyr" / "scripts" / "west_commands"))

pytest_plugins = ["pytest_pouch.plugin"]

import pytest  # noqa: E402

from pytest_pouch.plugin import generate_device_credentials  # noqa: E402
from runners.core import BuildConfiguration  # noqa: E402
from twister_harness.device.device_adapter import DeviceAdapter  # noqa: E402
from twister_harness.twister_harness_config import TwisterHarnessConfig  # noqa: E402


def pytest_addoption(parser):
    parser.addoption(
        "--gateway-cloud",
        action="store_true",
        default=False,
        help="Create a Golioth cloud device for the gateway",
    )
    parser.addoption(
        "--target-domain",
        type=str,
        default=None,
        help="Sysbuild sub-image whose build tree is the Pouch DUT",
    )
    parser.addoption(
        "--ota-image-size",
        type=int,
        default=400 * 1024,
        help="OTA payload size in bytes",
    )


@pytest.fixture(scope="module")
async def gateway(request, project):
    """Golioth cloud identity for the gateway, or None for direct clients.

    Enabled by --gateway-cloud. Direct-client tests still request this
    fixture (transitively via `setup` and `gateway_creds`) but always see
    None, so no branching is needed at call sites beyond a `gateway or
    device` selection.
    """
    if not request.config.getoption("--gateway-cloud"):
        yield None
        return

    name = "generated-gw-" + "".join(
        random.choice(string.ascii_letters) for _ in range(16)
    )
    if request.config.getoption("--mask-secrets"):
        print(f"::add-mask::{name}")
    gw = await project.create_device(name, name)

    yield gw

    await project.delete_device(gw)


def determine_scope(fixture_name, config):
    if dut_scope := config.getoption("--dut-scope", None):
        return dut_scope
    return "function"


@pytest.fixture(scope=determine_scope)
def dut(
    gateway_creds,
    request: pytest.FixtureRequest,
    device_object: DeviceAdapter,
) -> Generator[DeviceAdapter, None, None]:
    """Launch the DUT via 'west flash -d <build>'.

    For direct-client examples this runs the single native_sim binary.
    For the gateway example it launches every sysbuild/BabbleSim domain
    (gateway, peripheral, PHY, ...) at once.
    """
    device_object.initialize_log_files(request.node.name)
    try:
        device_object.command = [
            device_object.west,
            "flash",
            "-d",
            str(device_object.device_config.build_dir),
        ]
        device_object.process_kwargs["cwd"] = str(device_object.device_config.build_dir)

        device_object.launch()
        yield device_object
    finally:
        device_object.close()


@pytest.fixture(scope="module")
def target_build_dir(twister_harness_config: TwisterHarnessConfig, request):
    """Build tree of the Pouch DUT image.

    For sysbuild examples the sample.yaml passes --target-domain naming
    the sub-image whose .config drives credentials, OTA package name,
    and pouch identity DER filenames used in the shared creds/ tree.
    """
    build_dir = Path(twister_harness_config.devices[0].build_dir)
    domain = request.config.getoption("--target-domain")
    return build_dir / domain if domain else build_dir


@pytest.fixture(scope="module")
def gateway_build_dir(twister_harness_config: TwisterHarnessConfig):
    """Build tree of the gateway sub-image, or None if none is present."""
    build_dir = Path(twister_harness_config.devices[0].build_dir)
    return next(
        (
            build_dir / name
            for name in ("gateway", "gateway_custom_connect")
            if (build_dir / name).exists()
        ),
        None,
    )


@pytest.fixture(scope="module")
def gateway_build_conf(gateway_build_dir):
    """BuildConfiguration for the gateway sub-image, or None."""
    if gateway_build_dir is None:
        return None
    return BuildConfiguration(str(gateway_build_dir))


@pytest.fixture(scope="module")
async def creds(creds_dir, ca, device, project, build_conf):
    """Peripheral/device credentials, signed against the shared CA.

    Overrides the plugin `creds` fixture to read the pouch identity DER
    filenames from Kconfig so multiple devices can coexist in one
    shared creds/ directory without stepping on each other's DER files.
    Falls back to the plugin defaults (crt.der / key.der) when the
    Kconfigs are absent.
    """
    crt_name = build_conf.get("CONFIG_EXAMPLE_POUCH_DEVICE_CRT_FILENAME", "crt.der")
    key_name = build_conf.get("CONFIG_EXAMPLE_POUCH_DEVICE_KEY_FILENAME", "key.der")

    generate_device_credentials(
        creds_dir,
        device.name,
        project.id,
        ca.key,
        ca.cert,
        crt_der_name=crt_name,
        key_der_name=key_name,
    )

    logging.info("Upload root public key to Golioth server")
    with open(ca.cert, "rb") as f:
        cert_pem = f.read()
    root_cert = await project.certificates.add(cert_pem, "root")
    yield root_cert["data"]["id"]
    await project.certificates.delete(root_cert["data"]["id"])


@pytest.fixture(scope="module")
async def gateway_creds(gateway, ca, creds_dir, project, gateway_build_conf):
    """Gateway DTLS credentials signed by the shared CA, into the shared
    creds/ directory under gateway-specific DER filenames.

    Runs only when a gateway cloud identity is enabled.
    """
    if gateway is None:
        return

    crt_name = gateway_build_conf.get(
        "CONFIG_EXAMPLE_POUCH_DEVICE_CRT_FILENAME", "crt.der"
    )
    key_name = gateway_build_conf.get(
        "CONFIG_EXAMPLE_POUCH_DEVICE_KEY_FILENAME", "key.der"
    )

    generate_device_credentials(
        creds_dir,
        gateway.name,
        project.id,
        ca.key,
        ca.cert,
        crt_der_name=crt_name,
        key_der_name=key_name,
    )


async def delete_led_setting(device):
    settings = await device.settings.get_all()
    for setting in settings:
        if "deviceId" in setting and setting["key"] == "LED":
            await device.settings.delete(setting["key"])


@pytest.fixture(scope="module", autouse=True)
async def setup(project, device, gateway, creds):
    devices = [d for d in (device, gateway) if d is not None]

    logging.info("Delete existing device-level LED settings")
    for d in devices:
        await delete_led_setting(d)

    logging.info("Ensure the project-level LED setting exists")
    await project.settings.set("LED", False)

    yield

    logging.info("Delete any existing device-level LED settings (cleanup)")
    for d in devices:
        await delete_led_setting(d)


@pytest.fixture(scope="module")
def settings_device(device, gateway):
    return gateway or device


@pytest.fixture(scope="module")
def test_id():
    """Unique random slug for this test module's cloud resources.

    Generated freshly per pytest module so that parallel CI pipelines,
    sequential retries, and concurrent local runs never collide on
    shared Golioth resources (cohorts, deployments, OTA artifacts).
    """
    return secrets.token_hex(8)


@pytest.fixture(scope="module")
def artifacts_to_cleanup():
    return []


@pytest.fixture(scope="module")
async def cohort(project, device, test_id, artifacts_to_cleanup):
    """Create a per-device, per-test cohort so OTA deployments are isolated.

    In the gateway example the `device` fixture resolves to the peripheral
    Golioth device (the OTA target). The gateway proxies OTA traffic
    between the peripheral and Golioth over the Pouch BLE tunnel.
    """
    cohort_name = f"{device.name.lower().replace('-', '_')}_{test_id}"
    logging.info("Creating cohort '%s' for device '%s'", cohort_name, device.name)
    cohort = await project.cohorts.create(cohort_name)
    await device.update_cohort(cohort.id)

    yield cohort

    try:
        await device.remove_cohort()
    except Exception:
        pass

    try:
        await project.cohorts.delete(cohort.id)
    except Exception:
        logging.warning("Cohort %s could not be deleted", cohort_name)

    for artifact_id in artifacts_to_cleanup:
        try:
            await project.artifacts.delete(artifact_id)
        except Exception:
            logging.warning("Artifact %s could not be deleted", artifact_id)


@pytest.fixture(scope="module")
def build_conf(target_build_dir):
    """BuildConfiguration for the Pouch DUT image (see target_build_dir)."""
    return BuildConfiguration(str(target_build_dir))


@pytest.fixture(scope="module")
def fw_update_component(build_conf):
    """Package name for the OTA artifact, as configured in the built firmware.

    Reads CONFIG_EXAMPLE_FW_UPDATE_COMPONENT from the DUT's generated
    .config so the pytest side always agrees with what the C side was
    built with, instead of duplicating the string here.
    """
    return build_conf["CONFIG_EXAMPLE_FW_UPDATE_COMPONENT"]


@pytest.fixture(scope="module")
def ota_image_size(request):
    return request.config.getoption("--ota-image-size")


@pytest.fixture(scope="module")
async def ota_firmware(
    project,
    device,
    cohort,
    test_id,
    tmp_path_factory,
    artifacts_to_cleanup,
    fw_update_component,
    ota_image_size,
):
    """Generate a random firmware image, upload as artifact, deploy to cohort."""
    # Version includes test_id (a random slug) so parallel pipelines,
    # retries, and concurrent local runs never collide on this artifact.
    version = f"2.0.0-{device.name}-{test_id}"
    image_data = os.urandom(ota_image_size)
    expected_sha256 = hashlib.sha256(image_data).hexdigest()

    tmp_path = tmp_path_factory.mktemp("ota")
    image_path = tmp_path / "firmware.bin"
    image_path.write_bytes(image_data)

    logging.info(
        "Uploading OTA artifact: %d bytes, SHA256=%s",
        ota_image_size,
        expected_sha256,
    )
    artifact = await project.artifacts.upload(
        path=image_path,
        version=version,
        package=fw_update_component,
    )
    artifacts_to_cleanup.append(artifact.id)

    logging.info("Creating deployment on cohort '%s'", cohort.name)
    await cohort.deployments.create(f"ota-test-{device.name}-{test_id}", [artifact.id])

    yield expected_sha256
