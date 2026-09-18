#
# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

import logging
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace

import anyio
import pytest

logger = logging.getLogger(__name__)

_OTA_MODES = {
    "firmware": "default; real update binary",
    "dummy": "random bytes, SHA256 verification",
    "disabled": "deselect all OTA-marked tests",
}
_OTA_MODES_DEFAULT = "firmware"
_OTA_MARKER_VALUES = frozenset(_OTA_MODES) - {"disabled"}
_OTA_MODES_VALID_STR = ", ".join(repr(m) for m in sorted(_OTA_MARKER_VALUES))


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

    ### OTA Flags ###

    parser.addoption(
        "--fw-update-bin",
        type=str,
        help="Path to the firmware update binary for OTA tests",
    )
    parser.addoption(
        "--fw-update-ver",
        type=str,
        help="Version string of the firmware update binary for OTA tests",
    )
    parser.addoption(
        "--fw-update-pkg-name",
        type=str,
        help="OTA package name for the firmware update artifact (required for OTA tests)",
    )

    mode_values_str = ", ".join(
        f"'{mode}' ({desc})" for mode, desc in _OTA_MODES.items()
    )
    parser.addoption(
        "--ota-mode",
        choices=tuple(_OTA_MODES),
        default=_OTA_MODES_DEFAULT,
        help=f"OTA test mode: {mode_values_str}",
    )


def pytest_configure(config):
    config.addinivalue_line(
        "markers",
        "ota_mode(mode): restrict the test to a specific OTA mode "
        f"({_OTA_MODES_VALID_STR}); respected when --ota-mode is one of "
        f"({_OTA_MODES_VALID_STR}). When --ota-mode=disabled, all "
        "OTA-marked tests are deselected.",
    )


def _ota_mode_extract_and_validate(item) -> str | None:
    marker = item.get_closest_marker("ota_mode")
    if marker is None:
        return None
    if len(marker.args) != 1:
        raise pytest.UsageError(
            f"Invalid ota_mode marker for {item.nodeid}: "
            f"expected exactly one argument, got {len(marker.args)}"
        )
    if len(marker.kwargs) != 0:
        raise pytest.UsageError(
            f"Invalid ota_mode marker for {item.nodeid}: "
            f"expected zero kwargs, got {len(marker.kwargs)}"
        )
    if marker.args[0] not in _OTA_MARKER_VALUES:
        raise pytest.UsageError(
            f"Invalid ota_mode marker for {item.nodeid}: "
            f"'{marker.args[0]!r}' not a valid option ({_OTA_MODES_VALID_STR})"
        )
    return marker.args[0]


def pytest_collection_modifyitems(config, items):
    selected = config.getoption("--ota-mode")
    keep: list = []
    drop: list = []
    for item in items:
        mode = _ota_mode_extract_and_validate(item)
        if mode is None or mode == selected:
            keep.append(item)
        else:
            drop.append(item)
    if drop:
        config.hook.pytest_deselected(items=drop)
    items[:] = keep


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
async def ca(creds_dir):
    """Generate a shared CA, with PEM files for signing and DER for firmware."""
    creds_dir.mkdir(mode=0o755, exist_ok=True, parents=True)

    logger.info("Generate CA private key and cert")

    await anyio.run_process(
        [
            "openssl",
            "ecparam",
            "-name",
            "prime256v1",
            "-genkey",
            "-noout",
            "-out",
            "ca.key.pem",
        ],
        check=True,
        cwd=creds_dir,
        stdout=None,
        stderr=None,
    )
    await anyio.run_process(
        [
            "openssl",
            "req",
            "-x509",
            "-new",
            "-nodes",
            "-key",
            "ca.key.pem",
            "-sha256",
            "-subj",
            "/C=US/CN=Root CA",
            "-days",
            "14",
            "-out",
            "ca.crt.pem",
        ],
        check=True,
        cwd=creds_dir,
        stdout=None,
        stderr=None,
    )

    await anyio.run_process(
        ["openssl", "x509", "-in", "ca.crt.pem", "-outform", "DER", "-out", "ca.der"],
        check=True,
        cwd=creds_dir,
        stdout=None,
        stderr=None,
    )

    return SimpleNamespace(
        key=creds_dir / "ca.key.pem",
        cert=creds_dir / "ca.crt.pem",
        der=creds_dir / "ca.der",
    )


async def generate_device_credentials(
    dest_dir: Path,
    device_name: str,
    project_id: str,
    ca_key: Path,
    ca_cert: Path,
    *,
    crt_der_name: str = "crt.der",
    key_der_name: str = "key.der",
) -> None:
    """Sign temporary device credentials and publish only the named DER files.

    The CA files remain in their original directory; this does not copy ca.der.
    """
    dest_dir.mkdir(mode=0o755, exist_ok=True, parents=True)

    logger.info("Generate device private key, csr and cert for '%s'", device_name)

    with TemporaryDirectory() as work_dir:
        await anyio.run_process(
            [
                "openssl",
                "ecparam",
                "-name",
                "prime256v1",
                "-genkey",
                "-noout",
                "-out",
                "key.pem",
            ],
            check=True,
            cwd=work_dir,
            stdout=None,
            stderr=None,
        )
        await anyio.run_process(
            [
                "openssl",
                "req",
                "-new",
                "-key",
                "key.pem",
                "-subj",
                f"/C=US/O={project_id}/CN={device_name}",
                "-out",
                "csr.pem",
            ],
            check=True,
            cwd=work_dir,
            stdout=None,
            stderr=None,
        )
        await anyio.run_process(
            [
                "openssl",
                "x509",
                "-req",
                "-in",
                "csr.pem",
                "-CA",
                str(ca_cert.resolve()),
                "-CAkey",
                str(ca_key.resolve()),
                "-CAserial",
                "ca.srl",
                "-CAcreateserial",
                "-out",
                "crt.pem",
                "-days",
                "500",
                "-sha256",
            ],
            check=True,
            cwd=work_dir,
            stdout=None,
            stderr=None,
        )

        logger.info("Convert key and cert to DER format")

        await anyio.run_process(
            [
                "openssl",
                "x509",
                "-in",
                "crt.pem",
                "-outform",
                "DER",
                "-out",
                str((dest_dir / crt_der_name).resolve()),
            ],
            check=True,
            cwd=work_dir,
            stdout=None,
            stderr=None,
        )
        await anyio.run_process(
            [
                "openssl",
                "ec",
                "-in",
                "key.pem",
                "-outform",
                "DER",
                "-out",
                str((dest_dir / key_der_name).resolve()),
            ],
            check=True,
            cwd=work_dir,
            stdout=None,
            stderr=None,
        )


@pytest.fixture(scope="module")
async def creds(creds_dir, ca, device, project):
    await generate_device_credentials(
        creds_dir, device.name, project.id, ca.key, ca.cert
    )

    logger.info("Upload root public key to Golioth server")

    cert_pem = await anyio.Path(ca.cert).read_bytes()

    root_cert = await project.certificates.add(cert_pem, "root")
    yield root_cert["data"]["id"]

    await project.certificates.delete(root_cert["data"]["id"])
