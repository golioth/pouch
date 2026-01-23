import pytest
import argparse
import os
from pathlib import Path
import random
import string
import subprocess
import trio

from pytest_hil.frdmrw612 import FRDMRW612

parser = argparse.ArgumentParser()

SERVER_NAME = "pouch_hil"


@pytest.fixture(scope="session")
def anyio_backend():
    return "trio"


def rand_str():
    return ''.join(random.choice(string.ascii_uppercase + string.ascii_lowercase) for i in range(16))


def pytest_addoption(parser):
    parser.addoption("--gw-board", action="store", help="Gateway hil_board")
    parser.addoption("--gw-port", action="store", help="Gateway serial port")
    parser.addoption("--gw-fw-image", action="store", help="Gateway firmware binary")
    parser.addoption("--gw-serial-number", type=str, action="store", help="Gateway programmer serial number")


class DeviceCredential:
    def __init__(self, name, psk_id, psk):
        self.name = name
        self.psk_id = psk_id
        self.psk = psk


@pytest.fixture(scope="module")
async def certificate_cred(request, project):
    # TODO: generate certificate here
    device_name = 'salmon-itchy-bobcat'

    yield DeviceCredential(
        f'{device_name}',
        f"{Path(request.config.rootdir, f'{device_name}.key.der')}",
        f"{Path(request.config.rootdir, f'{device_name}.crt.der')}",
    )

    # TODO remove generated certificate here


@pytest.fixture(scope="module")
async def gateway(request, project):
    assert request.config.getoption("--gw-board") == "frdm_rw612"

    gw = FRDMRW612(
        request.config.getoption("--gw-port"),
        115200,
        None,
        None,
        request.config.getoption("--gw-fw-image"),
        serial_number=request.config.getoption("--gw-serial-number"),
    )

    name = f'gateway-{rand_str()}'
    gw_device = await project.create_device(name, name)
    gw_credentials = await gw_device.credentials.add(name, name)

    async with gw.started():
        await gw.set_golioth_psk_credentials(
            gw_credentials["identity"],
            gw_credentials["preSharedKey"],
        )

        await gw.send_cmd("kernel reboot")

        assert None is not await gw.wait_for_regex_in_line(
            ".*Scanning successfully started", timeout_s=120
        )
        yield gw

    await project.delete_device(gw_device)


@pytest.fixture(scope="module")
async def ble_device(board, certificate_cred, gateway, project):
    await board.wait_for_regex_in_line('.*Failed to load certificate', timeout_s=180.0)

    await board.send_cmd('log halt')

    # Set Golioth credential

    subprocess.run(
        [
            "smpmgr",
            "--port",
            board.port,
            "--mtu",
            "128",
            "file",
            "upload",
            certificate_cred.psk_id,
            "/lfs1/credentials/key.der",
        ]
    )
    subprocess.run(
        [
            "smpmgr",
            "--port",
            board.port,
            "--mtu",
            "128",
            "file",
            "upload",
            certificate_cred.psk,
            "/lfs1/credentials/crt.der",
        ]
    )

    await board.send_cmd('log go')
    await board.send_cmd('kernel reboot')

    assert None is not await board.wait_for_regex_in_line(".*glth_dispatch: Beginning Golioth Uplink", timeout_s=180.0)

    await trio.sleep(10) # Time for first uplink to arrive at server
    ble_device = await project.device_by_name(certificate_cred.name)

    yield ble_device

    await project.delete_device_by_name(certificate_cred.name)
