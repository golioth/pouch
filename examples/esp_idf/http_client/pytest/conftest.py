#
# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

import os
import secrets
import sys
from dataclasses import dataclass
from pathlib import Path

import pytest

# Patch esptool v4.* for underscore/dash conflict with v5.*
import esptool_v4_shim  # noqa: F401

sys.path.insert(
    0, str(Path(__file__).resolve().parents[4] / "scripts" / "pytest-pouch")
)

pytest_plugins = ["pytest_pouch.plugin"]


@dataclass
class WifiCreds:
    ssid: str
    psk: str


@dataclass
class DeviceCertBundle:
    crt_der: bytes
    key_der: bytes


@dataclass
class ProvisioningCreds:
    wifi: WifiCreds
    certs: DeviceCertBundle


def pytest_addoption(parser):
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
    parser.addoption(
        "--generate-certs",
        action="store_true",
        default=False,
        help="Generate test PKI credentials during test setup",
    )


def _option_or_env(request, option_name: str, env_name: str) -> str | None:
    return request.config.getoption(option_name) or os.getenv(env_name)


def _device_name_from_cert_cn(cert_der: bytes | None = None) -> str | None:
    if cert_der is None:
        cert_der_path = os.getenv("DEVICE_CRT_DER_PATH")
        if not cert_der_path:
            return None
        try:
            cert_der = Path(cert_der_path).read_bytes()
        except OSError:
            return None

    if not cert_der:
        return None

    try:
        from cryptography import x509
        from cryptography.x509.oid import NameOID

        cert = x509.load_der_x509_certificate(cert_der)
        common_names = cert.subject.get_attributes_for_oid(NameOID.COMMON_NAME)
        if not common_names:
            return None

        cn = common_names[0].value
        if isinstance(cn, bytes):
            cn = cn.decode("utf-8", errors="ignore")
        cn = cn.strip()
        return cn or None
    except Exception:
        return None


@pytest.fixture(scope="module")
def wifi_creds(request) -> WifiCreds:
    wifi_ssid = _option_or_env(request, "--wifi-ssid", "WIFI_SSID")
    wifi_psk = _option_or_env(request, "--wifi-psk", "WIFI_PSK")

    missing = []
    if not wifi_ssid:
        missing.append("WIFI_SSID/--wifi-ssid")
    if not wifi_psk:
        missing.append("WIFI_PSK/--wifi-psk")
    if missing:
        pytest.fail(f"Missing provisioning inputs: {', '.join(missing)}")

    return WifiCreds(ssid=wifi_ssid, psk=wifi_psk)


@pytest.fixture(scope="module")
def device_name(request) -> str:
    configured_device_name = _option_or_env(
        request, "--device-name", "GOLIOTH_DEVICE_NAME"
    )

    if configured_device_name is not None and configured_device_name != "":
        return configured_device_name

    if request.config.getoption("--generate-certs"):
        return f"esp-idf-http-client-{secrets.token_hex(4)}"

    device_name = _device_name_from_cert_cn()
    if not device_name:
        pytest.fail(
            "Missing cloud device identity. Set GOLIOTH_DEVICE_NAME or provide "
            "DEVICE_CRT_DER_PATH with a certificate CN matching the cloud device name"
        )

    return device_name


@pytest.fixture(scope="module")
async def device(project, device_name):
    created = False

    devices = await project.get_devices({"deviceName": device_name})
    if devices:
        cloud_device = devices[0]
    else:
        cloud_device = await project.create_device(device_name, device_name)
        created = True

    yield cloud_device

    if created:
        await project.delete_device(cloud_device)


@pytest.fixture(scope="module")
def cloud_config(request, device_name) -> dict[str, str]:
    api_url = _option_or_env(request, "--api-url", "GOLIOTH_API_URL")
    if not api_url:
        api_url = "https://api.golioth.io"

    api_key = _option_or_env(request, "--api-key", "GOLIOTH_API_KEY")
    if not api_key:
        pytest.fail("Missing cloud input: GOLIOTH_API_KEY/--api-key")

    return {
        "api_url": api_url,
        "api_key": api_key,
        "device_name": device_name,
    }


@pytest.fixture(scope="module")
def cert_paths(request, creds_dir):
    if request.config.getoption("--generate-certs"):
        request.getfixturevalue("creds")

    return {
        "crt_der_path": creds_dir / "crt.der",
        "key_der_path": creds_dir / "key.der",
    }


@pytest.fixture(scope="module")
def device_cert_bundle(request, cert_paths) -> DeviceCertBundle:
    crt_der = b""
    key_der = b""

    if request.config.getoption("--generate-certs"):
        crt_der = Path(cert_paths["crt_der_path"]).read_bytes()
        key_der = Path(cert_paths["key_der_path"]).read_bytes()
    else:
        crt_path = os.getenv("DEVICE_CRT_DER_PATH")
        key_path = os.getenv("DEVICE_KEY_DER_PATH")

        missing = []
        if not crt_path:
            missing.append("DEVICE_CRT_DER_PATH")
        if not key_path:
            missing.append("DEVICE_KEY_DER_PATH")
        if missing:
            pytest.fail(
                "Missing certificate inputs. Set DEVICE_CRT_DER_PATH and "
                f"DEVICE_KEY_DER_PATH. Missing: {', '.join(missing)}"
            )

        try:
            crt_der = Path(crt_path).read_bytes()
        except OSError as exc:
            pytest.fail(f"Unable to read DEVICE_CRT_DER_PATH '{crt_path}': {exc}")
        try:
            key_der = Path(key_path).read_bytes()
        except OSError as exc:
            pytest.fail(f"Unable to read DEVICE_KEY_DER_PATH '{key_path}': {exc}")

    return DeviceCertBundle(crt_der=crt_der, key_der=key_der)


@pytest.fixture(scope="module")
def provisioning_creds(wifi_creds, device_cert_bundle) -> ProvisioningCreds:
    return ProvisioningCreds(
        wifi=wifi_creds,
        certs=device_cert_bundle,
    )
