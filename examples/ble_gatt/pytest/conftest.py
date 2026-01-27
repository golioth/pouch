import pytest
import argparse
import os
from pathlib import Path
import random
import string
import subprocess

from pytest_hil.frdmrw612 import FRDMRW612

parser = argparse.ArgumentParser()


@pytest.fixture(scope="session")
def anyio_backend():
    return "trio"


def pytest_addoption(parser):
    parser.addoption("--gw-board", action="store", help="Gateway hil_board")
    parser.addoption("--gw-port", action="store", help="Gateway serial port")
    parser.addoption("--gw-fw-image", action="store", help="Gateway firmware binary")
    parser.addoption("--gw-serial-number", type=str, action="store", help="Gateway programmer serial number")

def rand_str():
    return ''.join(random.choice(string.ascii_uppercase + string.ascii_lowercase) for i in range(16))

@pytest.fixture(scope="module")
async def certificate_cred(request, project):
    device_name = f"ble_{rand_str()}"
    gateway_name = f"gw_{rand_str()}"

    # Check cloud to verify device does not exist

    with pytest.raises(Exception):
        device = await project.device_by_name(device_name)

    subprocess.run(
        [
            "openssl",
            "ecparam",
            "-name",
            "prime256v1",
            "-genkey",
            "-noout",
            "-out",
            f"{gateway_name}.key.pem",
        ],
        cwd=request.config.rootdir,
    )

    subprocess.run(
        [
            "openssl",
            "req",
            "-x509",
            "-new",
            "-nodes",
            "-key",
            f'{gateway_name}.key.pem',
            "-sha256",
            "-subj",
            f"/CN={gateway_name} CA",
            "-days",
            "10",
            "-out",
            f'{gateway_name}.crt.pem',
        ],
        cwd=request.config.rootdir,
    )

    # Pass root public key to Golioth server

    with open(f"{gateway_name}.crt.pem", "rb") as f:
        cert_pem = f.read()
    root_cert = await project.certificates.add(cert_pem, "root")

    # Device Cert

    subprocess.run(
        [
            "openssl",
            "ecparam",
            "-name",
            "prime256v1",
            "-genkey",
            "-noout",
            "-out",
            f"{device_name}.key.pem",
        ],
        cwd=request.config.rootdir,
    )

    subprocess.run(
        [
            "openssl",
            "req",
            "-new",
            "-key",
            f"{device_name}.key.pem",
            "-subj",
            f"/O={project.info['id']}/CN={device_name}",
            "-out",
            f"{device_name}.csr.pem",
        ],
        cwd=request.config.rootdir,
    )

    subprocess.run(
        [
            "openssl",
            "x509",
            "-req",
            "-in",
            f"{device_name}.csr.pem",
            "-CA",
            f'{gateway_name}.crt.pem',
            "-CAkey",
            f'{gateway_name}.key.pem',
            "-CAcreateserial",
            "-out",
            f"{device_name}.crt.pem",
            "-days",
            "7",
            "-sha256",
        ],
        cwd=request.config.rootdir,
    )

    subprocess.run(
        [
            "openssl",
            "ec",
            "-in",
            f"{device_name}.key.pem",
            "-outform",
            "DER",
            "-out",
            f"{device_name}.key.der",
        ],
        cwd=request.config.rootdir,
    )

    subprocess.run(
        [
            "openssl",
            "x509",
            "-in",
            f"{device_name}.crt.pem",
            "-outform",
            "DER",
            "-out",
            f"{device_name}.crt.der",
        ],
        cwd=request.config.rootdir,
    )

    yield (f"{Path(request.config.rootdir, f"{device_name}.key.der")}", f"{Path(request.config.rootdir, f"{device_name}.crt.der")}")

    await project.certificates.delete(root_cert["data"]["id"])

    for p in Path(request.config.rootdir).glob(f"{gateway_name}.*"):
        p.unlink()
    for p in Path(request.config.rootdir).glob(f"{device_name}.*"):
        p.unlink()


@pytest.fixture(scope="module")
async def gateway(request):
    assert request.config.getoption("--gw-board") == "frdm_rw612"
    assert request.config.getoption("--gw-fw-image") == "gateway-frdm_rw612.hex"
    assert request.config.getoption("--gw-serial-number") == "1066907334"

    gw = FRDMRW612(
        request.config.getoption("--gw-port"),
        115200,
        None,
        None,
        request.config.getoption("--gw-fw-image"),
        serial_number=request.config.getoption("--gw-serial-number"),
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
