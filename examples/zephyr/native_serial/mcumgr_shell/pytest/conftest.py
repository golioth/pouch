#
# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

import shutil
import subprocess
import tempfile
import time
from pathlib import Path
from typing import Generator

import pytest

from twister_harness.device.device_adapter import DeviceAdapter


@pytest.fixture(scope="function")
def socat_pair():
    """Create a socat virtual null-modem pair with deterministic symlink paths."""
    tmpdir = Path(tempfile.mkdtemp(prefix="zephyr_mcumgr_shell_"))
    port_server = tmpdir / "port_server"
    port_client = tmpdir / "port_client"

    proc = subprocess.Popen(
        [
            "socat",
            f"PTY,link={port_server},raw,echo=0",
            f"PTY,link={port_client},raw,echo=0",
        ],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )

    # Wait for symlinks to appear
    deadline = time.time() + 5
    while not (port_server.exists() and port_client.exists()):
        time.sleep(0.05)
        if time.time() > deadline:
            proc.terminate()
            proc.wait()
            shutil.rmtree(tmpdir, ignore_errors=True)
            raise TimeoutError("socat PTY pair creation timed out")

    yield {"server_port": port_server, "client_port": port_client, "tmpdir": tmpdir}

    proc.terminate()
    proc.wait()
    shutil.rmtree(tmpdir, ignore_errors=True)


@pytest.fixture(scope="function")
def server_process(device_object: DeviceAdapter, socat_pair: dict):
    """Launch the MCUmgr shell server as a background process."""
    # Extract server exe path via generate_command(), then reset for dut.
    device_object.generate_command()
    server_exe = device_object.command[0]
    device_object.command = []

    proc = subprocess.Popen(
        [
            server_exe,
            f"-uart_1_port={socat_pair['server_port']}",
        ],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )

    # Give the server time to boot and register MCUmgr + shell handlers.
    time.sleep(1.0)

    yield proc

    proc.terminate()
    proc.wait()


@pytest.fixture(scope="function")
def dut(
    request: pytest.FixtureRequest,
    device_object: DeviceAdapter,
    socat_pair: dict,
    server_process: subprocess.Popen,
) -> Generator[DeviceAdapter, None, None]:
    """Launch the MCUmgr shell client as the foreground dut.

    Depends on server_process to ensure the server is running first.
    """
    device_object.initialize_log_files(request.node.name)

    build_dir = Path(device_object.device_config.build_dir)
    client_exe = build_dir / "client" / "zephyr" / "zephyr.exe"

    if not client_exe.exists():
        pytest.skip(f"Client executable not found at {client_exe}")

    # Override device_object command to launch the client instead of server.
    device_object.command = [
        str(client_exe),
        "-uart_stdinout",
        f"-uart_1_port={socat_pair['client_port']}",
    ]

    try:
        device_object.launch()
        yield device_object
    finally:
        device_object.close()
