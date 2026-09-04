#
# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

import logging
import re
import subprocess
import time
from pathlib import Path

import pytest

logger = logging.getLogger(__name__)

_SIMULATOR_PLATFORMS = {
    "native_sim",
    "native_sim/native",
    "native_sim/native/64",
    "nrf52_bsim/native",
}


def _read_kconfig_value(config_path: Path, key: str) -> str | None:
    """Read a string Kconfig value from a Zephyr build's ``.config``."""
    if not config_path.exists():
        return None
    pattern = re.compile(rf'^{re.escape(key)}="([^"]*)"$', re.MULTILINE)
    match = pattern.search(config_path.read_text())
    return match.group(1) if match else None


def _twister_out_dir(device_object) -> Path:
    """Find the Twister output directory containing this test build."""
    build_dir = Path(device_object.device_config.build_dir)
    for candidate in (build_dir, *build_dir.parents):
        if (candidate / "testplan.json").is_file():
            return candidate
    pytest.fail(f"Could not locate Twister output directory above {build_dir}")


def _fw_update_bins_root(device_object) -> Path:
    """Compute the fw_update_bins extraction root for the current test's build."""
    return _twister_out_dir(device_object) / "fw_update_bins"


@pytest.fixture(scope="module")
def fw_update_bin(request, device_object) -> Path | None:
    """Resolve OTA update binary path from the twister build context.

    Overrides the default in ``ota_harness``. CLI override
    (``--fw-update-bin``) takes precedence when provided.
    """
    bin_cli = request.config.getoption("--fw-update-bin")
    if bin_cli:
        return Path(bin_cli)
    twister_root = _twister_out_dir(device_object)
    original_bin = (
        device_object.device_config.app_build_dir / "zephyr" / "zephyr.signed.bin"
    )
    return _fw_update_bins_root(device_object) / original_bin.relative_to(twister_root)


@pytest.fixture(scope="module")
def fw_update_version(request, device_object) -> str | None:
    """Resolve OTA update version from the twister build context.

    Overrides the default in ``ota_harness``. CLI override
    (``--fw-update-ver``) takes precedence when provided.
    """
    ver_cli = request.config.getoption("--fw-update-ver")
    if ver_cli:
        return ver_cli
    version_path = _fw_update_bins_root(device_object) / "update-ver-num.txt"
    if version_path.exists():
        return version_path.read_text().strip()
    return None


@pytest.fixture(scope="module")
def fw_update_package(request, device_object) -> str | None:
    """Resolve OTA package name from the twister build's ``.config``.

    Overrides the default in ``ota_harness``. CLI override
    (``--fw-update-pkg-name``) takes precedence when provided.
    """
    pkg_cli = request.config.getoption("--fw-update-pkg-name")
    if pkg_cli:
        return pkg_cli
    config_path = device_object.device_config.app_build_dir / "zephyr" / ".config"
    return _read_kconfig_value(config_path, "CONFIG_EXAMPLE_FW_UPDATE_COMPONENT")


def _upload_credentials(serial_port, creds_dir):
    logger.info("Uploading Pouch/HTTP credentials via smpmgr")
    for local_name, remote_path in [
        ("crt.der", "/lfs1/credentials/crt.der"),
        ("key.der", "/lfs1/credentials/key.der"),
    ]:
        subprocess.run(
            [
                "smpmgr",
                "--port",
                serial_port,
                "--mtu",
                "128",
                "file",
                "upload",
                str(creds_dir / local_name),
                remote_path,
            ],
            check=True,
        )

        logger.info("Uploaded %s -> %s", local_name, remote_path)


def _provision_hardware(device_object, creds_dir):
    serial_port = device_object.device_config.serial_configs[0].port
    build_dir = device_object.device_config.build_dir

    logger.info("Flashing firmware")
    flash_cmd = ["west", "flash", "--no-rebuild", "-d", str(build_dir)]

    west_flash_extra_args = device_object.device_config.west_flash_extra_args
    if west_flash_extra_args:
        flash_cmd.extend(["--", *west_flash_extra_args])

    subprocess.run(flash_cmd, check=True)

    logger.info("Waiting for device to boot")
    time.sleep(15)

    _upload_credentials(serial_port, creds_dir)

    # Device needs reboot; we rely on device_object.launch()


@pytest.fixture(scope="module")
def dut(request, device_object, creds_dir, creds):
    device_object.initialize_log_files(request.node.name)

    platform = device_object.device_config.platform
    if platform not in _SIMULATOR_PLATFORMS:
        _provision_hardware(device_object, creds_dir)

    device_object.launch()

    logger.info("Waiting for device to boot and load credentials")
    device_object.readlines_until(regex="Credentials loaded", timeout=60.0)

    yield device_object

    device_object.close()
