#
# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

import logging
import subprocess

import anyio
import pytest

_SIMULATOR_PLATFORMS = {
    "native_sim",
    "native_sim/native",
    "native_sim/native/64",
    "nrf52_bsim/native",
}


@pytest.fixture(scope="module", autouse=True)
async def provisioned_credentials(creds_dir, creds, request):
    try:
        twister_config = request.getfixturevalue("twister_harness_config")
    except pytest.FixtureLookupError:
        yield
        return

    platform = twister_config.devices[0].platform
    if platform in _SIMULATOR_PLATFORMS:
        yield
        return

    build_dir = twister_config.devices[0].build_dir
    serial_port = twister_config.devices[0].serial_configs[0].port

    logging.info("Flashing firmware")
    subprocess.run(
        ["west", "flash", "--no-rebuild", "-d", str(build_dir)],
        check=True,
    )

    logging.info("Waiting for device to boot")
    await anyio.sleep(15)

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

    logging.info("Resetting device")
    subprocess.run(
        [
            "smpmgr",
            "--port",
            serial_port,
            "os",
            "reset",
        ],
        check=True,
    )

    logging.info("Waiting for device to reboot")
    await anyio.sleep(5)

    yield


@pytest.fixture(scope="module")
def ensured_credentials(provisioned_credentials):
    pass
