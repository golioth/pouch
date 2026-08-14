#
# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

import logging
import subprocess
import time

import pytest

_SIMULATOR_PLATFORMS = {
    "native_sim",
    "native_sim/native",
    "native_sim/native/64",
    "nrf52_bsim/native",
}


def _upload_credentials(serial_port, creds_dir):
    logging.info("Uploading Pouch/HTTP credentials via smpmgr")
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

        logging.info("Uploaded %s -> %s", local_name, remote_path)


def _provision_hardware(device_object, creds_dir):
    serial_port = device_object.device_config.serial_configs[0].port
    build_dir = device_object.device_config.build_dir

    logging.info("Flashing firmware")
    flash_cmd = ["west", "flash", "--no-rebuild", "-d", str(build_dir)]

    west_flash_extra_args = device_object.device_config.west_flash_extra_args
    if west_flash_extra_args:
        flash_cmd.extend(["--", *west_flash_extra_args])

    subprocess.run(flash_cmd, check=True)

    logging.info("Waiting for device to boot")
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

    logging.info("Waiting for device to boot and load credentials")
    device_object.readlines_until(regex="Credentials loaded", timeout=60.0)

    yield device_object

    device_object.close()
