import pytest
import argparse
from pathlib import Path
import subprocess

from pytest_hil.frdmrw612 import FRDMRW612

parser = argparse.ArgumentParser()

SERVER_NAME = "pouch_hil"


@pytest.fixture(scope="session")
def anyio_backend():
    return "trio"


def pytest_addoption(parser):
    parser.addoption("--gw-board", action="store", help="Gateway hil_board")
    parser.addoption("--gw-port", action="store", help="Gateway serial port")
    parser.addoption("--gw-fw-image", action="store", help="Gateway firmware binary")


@pytest.fixture(scope="module")
async def gateway(request):
    assert request.config.getoption("--gw-board") == "frdm_rw612"

    gw = FRDMRW612(
        request.config.getoption("--gw-port"),
        115200,
        None,
        None,
        request.config.getoption("--gw-fw-image"),
        serial_number="1063461944",
    )

    async with gw.started():
        await gw.set_golioth_psk_credentials(
            "20250407152117-nrf9160dk@ble-private-access",
            "d738169b8e08d054b7594a2f399d7fca",
        )

        await gw.send_cmd("kernel reboot")

        assert None is not await gw.wait_for_regex_in_line(
            ".*Scanning successfully started", timeout_s=120
        )
        yield gw
