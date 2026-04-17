#
# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

import logging
from pathlib import Path
import subprocess

import pytest


@pytest.fixture(scope="session")
def anyio_backend():
    return "trio"


@pytest.fixture(scope="module")
def creds_dir(request: pytest.FixtureRequest):
    """Return directory for storing credential files.

    Override this fixture in a local conftest.py to change the path.
    """
    return Path(request.config.option.build_dir) / "creds"


@pytest.fixture(scope="module")
async def creds(creds_dir, device, project):
    creds_dir.mkdir(mode=0o755, exist_ok=True, parents=True)

    logging.info("Generate CA private key and cert")

    subprocess.run(
        "openssl ecparam -name prime256v1 -genkey -noout -out ca.key.pem",
        check=True,
        shell=True,
        cwd=creds_dir,
    )
    subprocess.run(
        """\
    openssl req -x509 -new -nodes \
        -key ca.key.pem \
        -sha256 -subj "/C=US/CN=Root CA" \
        -days 14 -out ca.crt.pem""",
        check=True,
        shell=True,
        cwd=creds_dir,
    )

    logging.info("Generate edge node private key, csr and cert")

    subprocess.run(
        f"openssl ecparam -name prime256v1 -genkey -noout -out {device.name}.key.pem",
        check=True,
        shell=True,
        cwd=creds_dir,
    )
    subprocess.run(
        f"""\
    openssl req -new \
        -key {device.name}.key.pem \
        -subj "/C=US/O={project.id}/CN={device.name}" \
        -out {device.name}.csr.pem""",
        check=True,
        shell=True,
        cwd=creds_dir,
    )
    subprocess.run(
        f"""\
    openssl x509 -req \
        -in "{device.name}.csr.pem" \
        -CA "ca.crt.pem" \
        -CAkey "ca.key.pem" \
        -CAcreateserial \
        -out "{device.name}.crt.pem" \
        -days 500 -sha256""",
        check=True,
        shell=True,
        cwd=creds_dir,
    )

    logging.info("Convert key and cert to DER format")

    subprocess.run(
        f"openssl x509 -in {device.name}.crt.pem -outform DER -out crt.der",
        check=True,
        shell=True,
        cwd=creds_dir,
    )
    subprocess.run(
        f"openssl ec -in {device.name}.key.pem -outform DER -out key.der",
        check=True,
        shell=True,
        cwd=creds_dir,
    )

    logging.info("Upload root public key to Golioth server")

    with open(creds_dir / "ca.crt.pem", "rb") as f:
        cert_pem = f.read()

    root_cert = await project.certificates.add(cert_pem, "root")
    yield root_cert["data"]["id"]

    await project.certificates.delete(root_cert["data"]["id"])
