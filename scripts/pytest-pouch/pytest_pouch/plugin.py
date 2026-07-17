#
# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

import logging
import subprocess
from pathlib import Path
from types import SimpleNamespace

import pytest


def pytest_addoption(parser):
    parser.addoption(
        "--generate-certs",
        action="store_true",
        default=False,
        help="Generate test PKI credentials during test setup",
    )
    parser.addoption(
        "--wifi-ssid",
        type=str,
        help="WiFi SSID",
    )
    parser.addoption(
        "--wifi-psk",
        type=str,
        help="WiFi PSK",
    )


@pytest.fixture(scope="session")
def anyio_backend():
    return "trio"


@pytest.fixture(scope="module")
def creds_dir(request: pytest.FixtureRequest):
    """Return directory for storing credential files.

    Override this fixture in a local conftest.py to change the path.
    """
    try:
        harness = request.getfixturevalue("twister_harness_config")
        return harness.devices[0].build_dir / "creds"
    except (pytest.FixtureLookupError, ImportError):
        return Path(request.config.option.build_dir) / "creds"


@pytest.fixture(scope="module")
def ca(creds_dir):
    """Shared CA key and certificate in ``creds_dir``.

    Emits ca.key.pem, ca.crt.pem, and ca.der so downstream fixtures can
    sign device certs (PEM) and firmware can load the CA in DER form.
    """
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
    subprocess.run(
        "openssl x509 -in ca.crt.pem -outform DER -out ca.der",
        check=True,
        shell=True,
        cwd=creds_dir,
    )

    return SimpleNamespace(
        key=creds_dir / "ca.key.pem",
        cert=creds_dir / "ca.crt.pem",
        der=creds_dir / "ca.der",
    )


def generate_device_credentials(
    dest_dir: Path,
    device_name: str,
    project_id: str,
    ca_key: Path,
    ca_cert: Path,
    *,
    crt_der_name: str = "crt.der",
    key_der_name: str = "key.der",
) -> None:
    """Sign a device key + cert against ``ca_*`` and write PEM+DER into ``dest_dir``.

    Writes:
      - ``{device_name}.key.pem``, ``.csr.pem``, ``.crt.pem``
      - ``<crt_der_name>`` (device cert, DER)
      - ``<key_der_name>`` (device key, DER)

    Does not emit ``ca.der``; the ``ca`` fixture writes that once into
    ``creds_dir``. Callers that place device DER files into a directory
    other than ``creds_dir`` are responsible for making the CA file
    available at their target path.
    """
    dest_dir.mkdir(mode=0o755, exist_ok=True, parents=True)

    logging.info("Generate device private key, csr and cert for '%s'", device_name)

    subprocess.run(
        f"openssl ecparam -name prime256v1 -genkey -noout -out {device_name}.key.pem",
        check=True,
        shell=True,
        cwd=dest_dir,
    )
    subprocess.run(
        f"""\
    openssl req -new \
        -key {device_name}.key.pem \
        -subj "/C=US/O={project_id}/CN={device_name}" \
        -out {device_name}.csr.pem""",
        check=True,
        shell=True,
        cwd=dest_dir,
    )
    subprocess.run(
        f"""\
    openssl x509 -req \
        -in "{device_name}.csr.pem" \
        -CA "{ca_cert}" \
        -CAkey "{ca_key}" \
        -CAcreateserial \
        -out "{device_name}.crt.pem" \
        -days 500 -sha256""",
        check=True,
        shell=True,
        cwd=dest_dir,
    )

    logging.info("Convert device key and cert to DER format")

    subprocess.run(
        f"openssl x509 -in {device_name}.crt.pem -outform DER -out {crt_der_name}",
        check=True,
        shell=True,
        cwd=dest_dir,
    )
    subprocess.run(
        f"openssl ec -in {device_name}.key.pem -outform DER -out {key_der_name}",
        check=True,
        shell=True,
        cwd=dest_dir,
    )


@pytest.fixture(scope="module")
async def creds(creds_dir, ca, device, project):
    generate_device_credentials(creds_dir, device.name, project.id, ca.key, ca.cert)

    logging.info("Upload root public key to Golioth server")

    with open(ca.cert, "rb") as f:
        cert_pem = f.read()

    root_cert = await project.certificates.add(cert_pem, "root")
    yield root_cert["data"]["id"]

    await project.certificates.delete(root_cert["data"]["id"])
