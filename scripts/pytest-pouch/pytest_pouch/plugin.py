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

    ca_key = creds_dir / "ca.key.pem"
    ca_crt = creds_dir / "ca.crt.pem"
    dev_key = creds_dir / f"{device.name}.key.pem"
    dev_csr = creds_dir / f"{device.name}.csr.pem"
    dev_crt = creds_dir / f"{device.name}.crt.pem"

    logging.info("Generate CA private key and cert")

    subprocess.run(
        [
            "openssl",
            "ecparam",
            "-name",
            "prime256v1",
            "-genkey",
            "-noout",
            "-out",
            str(ca_key),
        ],
        check=True,
    )
    subprocess.run(
        [
            "openssl",
            "req",
            "-x509",
            "-new",
            "-nodes",
            "-key",
            str(ca_key),
            "-sha256",
            "-subj",
            "/C=US/CN=Root CA",
            "-days",
            "14",
            "-out",
            str(ca_crt),
        ],
        check=True,
    )

    logging.info("Generate edge node private key, csr and cert")

    subprocess.run(
        [
            "openssl",
            "ecparam",
            "-name",
            "prime256v1",
            "-genkey",
            "-noout",
            "-out",
            str(dev_key),
        ],
        check=True,
    )
    subprocess.run(
        [
            "openssl",
            "req",
            "-new",
            "-key",
            str(dev_key),
            "-subj",
            f"/C=US/O={project.id}/CN={device.name}",
            "-out",
            str(dev_csr),
        ],
        check=True,
    )
    subprocess.run(
        [
            "openssl",
            "x509",
            "-req",
            "-in",
            str(dev_csr),
            "-CA",
            str(ca_crt),
            "-CAkey",
            str(ca_key),
            "-CAcreateserial",
            "-out",
            str(dev_crt),
            "-days",
            "500",
            "-sha256",
        ],
        check=True,
    )

    logging.info("Convert key and cert to DER format")

    subprocess.run(
        [
            "openssl",
            "x509",
            "-in",
            str(dev_crt),
            "-outform",
            "DER",
            "-out",
            str(creds_dir / "crt.der"),
        ],
        check=True,
    )
    subprocess.run(
        [
            "openssl",
            "ec",
            "-in",
            str(dev_key),
            "-outform",
            "DER",
            "-out",
            str(creds_dir / "key.der"),
        ],
        check=True,
    )

    logging.info("Upload root public key to Golioth server")

    with open(ca_crt, "rb") as f:
        cert_pem = f.read()

    root_cert = await project.certificates.add(cert_pem, "root")
    yield root_cert["data"]["id"]

    await project.certificates.delete(root_cert["data"]["id"])
