#
# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

import logging
import secrets
import subprocess
import time

import pytest


def pytest_addoption(parser):
    parser.addoption(
        "--gateway-port",
        type=str,
        help="Serial port for gateway provisioning",
    )


@pytest.fixture(scope="module")
def gateway_serial_port(request):
    port = request.config.getoption("--gateway-port")
    if not port:
        pytest.fail("--gateway-port not set")
    return port


@pytest.fixture(scope="module")
def gateway_device_name():
    return "hil-gw-" + secrets.token_hex(4)


@pytest.fixture(scope="module")
async def gateway_cloud_device(project, gateway_device_name):
    created = False

    devices = await project.get_devices({"deviceName": gateway_device_name})
    if devices:
        gw = devices[0]
    else:
        gw = await project.create_device(gateway_device_name, gateway_device_name)
        created = True

    yield gw

    if created:
        await project.delete_device(gw)


@pytest.fixture(scope="module")
def gateway_creds_dir(creds_dir):
    d = creds_dir / "gateway"
    d.mkdir(mode=0o755, exist_ok=True, parents=True)
    return d


@pytest.fixture(scope="module")
def gateway_creds(creds, creds_dir, gateway_creds_dir, gateway_cloud_device, project):
    """Generate gateway DTLS credentials using the same CA as the peripheral."""

    ca_key = (creds_dir / "ca.key.pem").resolve()
    ca_cert = (creds_dir / "ca.crt.pem").resolve()

    name = gateway_cloud_device.name

    logging.info("Generate gateway device private key and cert (signed by shared CA)")

    subprocess.run(
        f"openssl ecparam -name prime256v1 -genkey -noout -out {name}.key.pem",
        check=True,
        shell=True,
        cwd=gateway_creds_dir,
    )
    subprocess.run(
        f"""\
    openssl req -new \
        -key {name}.key.pem \
        -subj "/C=US/O={project.id}/CN={name}" \
        -out {name}.csr.pem""",
        check=True,
        shell=True,
        cwd=gateway_creds_dir,
    )
    subprocess.run(
        f"""\
    openssl x509 -req \
        -in "{name}.csr.pem" \
        -CA "{ca_cert}" \
        -CAkey "{ca_key}" \
        -CAcreateserial \
        -out "{name}.crt.pem" \
        -days 500 -sha256""",
        check=True,
        shell=True,
        cwd=gateway_creds_dir,
    )

    logging.info("Convert gateway key and cert to DER format")

    subprocess.run(
        f"openssl x509 -in {name}.crt.pem -outform DER -out crt.der",
        check=True,
        shell=True,
        cwd=gateway_creds_dir,
    )
    subprocess.run(
        f"openssl ec -in {name}.key.pem -outform DER -out key.der",
        check=True,
        shell=True,
        cwd=gateway_creds_dir,
    )


@pytest.fixture(scope="module", autouse=True)
def provisioned_gateway(gateway_serial_port, gateway_creds_dir, gateway_creds):
    logging.info("Uploading gateway credentials via smpmgr")

    for local_name, remote_path in [
        ("crt.der", "/lfs1/credentials/crt.der"),
        ("key.der", "/lfs1/credentials/key.der"),
    ]:
        subprocess.run(
            [
                "smpmgr",
                "--port",
                gateway_serial_port,
                "file",
                "upload",
                str(gateway_creds_dir / local_name),
                remote_path,
            ],
            check=True,
        )
        logging.info("Uploaded %s -> %s", local_name, remote_path)

    logging.info("Resetting gateway")
    subprocess.run(
        [
            "smpmgr",
            "--port",
            gateway_serial_port,
            "os",
            "reset",
        ],
        check=True,
    )

    logging.info("Waiting for gateway to reboot")
    time.sleep(30)

    yield
