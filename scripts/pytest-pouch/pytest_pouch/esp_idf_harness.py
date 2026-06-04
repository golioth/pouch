"""
Shared ESP-IDF pytest fixtures for pouch examples.

This module is intentionally opt-in: only test suites that include it in
their local ``pytest_plugins`` list will use these fixtures.

Usage notes:
- CLI options consumed by this module are registered in ``pytest_pouch.plugin``:
  ``--generate-certs``, ``--wifi-ssid``, and ``--wifi-psk``.
- Example-specific behavior is expected to be configured from each example
  ``conftest.py`` by overriding selected fixtures (for example,
  ``pouch_device_name_prefix`` and ``pouch_provision_wifi``).
"""

import os
import secrets
from dataclasses import dataclass
from pathlib import Path

import pytest


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
    certs: DeviceCertBundle
    wifi: WifiCreds | None = None


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
def pouch_device_name_prefix() -> str:
    """Prefix for generated cloud device names.

    This value is used only when ``--generate-certs`` is enabled and no
    explicit cloud device name is provided via ``--device-name`` or
    ``GOLIOTH_DEVICE_NAME``.

    Override this fixture from an example ``conftest.py`` to keep generated
    cloud device names sample-specific.
    """
    return "esp-idf"


@pytest.fixture(scope="module")
def pouch_provision_wifi() -> bool:
    """Control whether Wi-Fi credentials are required for provisioning.

    - ``False``: Wi-Fi credentials are not required and ``wifi_creds`` returns
      ``None``.
    - ``True``: Wi-Fi credentials are required from CLI options or env vars.

    Override this fixture from an example ``conftest.py`` when a sample needs
    Wi-Fi provisioning during the test flow.
    """
    return False


@pytest.fixture(scope="module")
def wifi_creds(request, pouch_provision_wifi) -> WifiCreds | None:
    """Resolve Wi-Fi credentials when Wi-Fi provisioning is enabled.

    Inputs are read from ``--wifi-ssid``/``--wifi-psk`` or
    ``WIFI_SSID``/``WIFI_PSK``.
    """
    if not pouch_provision_wifi:
        return None

    wifi_ssid = _option_or_env(request, "--wifi-ssid", "WIFI_SSID")
    wifi_psk = _option_or_env(request, "--wifi-psk", "WIFI_PSK")

    missing = []
    if not wifi_ssid:
        missing.append("WIFI_SSID/--wifi-ssid")
    if not wifi_psk:
        missing.append("WIFI_PSK/--wifi-psk")
    if missing:
        pytest.fail(f"Missing provisioning inputs: {', '.join(missing)}")

    return WifiCreds(ssid=str(wifi_ssid), psk=str(wifi_psk))


@pytest.fixture(scope="module")
def device_name(request, pouch_device_name_prefix) -> str:
    """Resolve cloud device name for this test session.

    Resolution order:
    1. ``--device-name`` or ``GOLIOTH_DEVICE_NAME``
    2. generated name (``<pouch_device_name_prefix>-<random>``) when
       ``--generate-certs`` is enabled
    3. certificate CN from ``DEVICE_CRT_DER_PATH``
    """
    configured_device_name = _option_or_env(
        request, "--device-name", "GOLIOTH_DEVICE_NAME"
    )

    if configured_device_name is not None and configured_device_name != "":
        return configured_device_name

    if request.config.getoption("--generate-certs"):
        return f"{pouch_device_name_prefix}-{secrets.token_hex(4)}"

    derived_device_name = _device_name_from_cert_cn()
    if not derived_device_name:
        pytest.fail(
            "Missing cloud device identity. Set GOLIOTH_DEVICE_NAME or provide "
            "DEVICE_CRT_DER_PATH with a certificate CN matching the cloud device name"
        )

    return derived_device_name


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
    """Return paths to DER certificate material used by tests.

    In generated-cert mode, this fixture triggers ``creds`` generation first.
    """
    if request.config.getoption("--generate-certs"):
        request.getfixturevalue("creds")

    return {
        "crt_der_path": creds_dir / "crt.der",
        "key_der_path": creds_dir / "key.der",
    }


@pytest.fixture(scope="module")
def device_cert_bundle(request, cert_paths) -> DeviceCertBundle:
    """Load device certificate and key DER bytes.

    Generated-cert mode reads files produced in ``creds_dir``. Manual mode
    reads ``DEVICE_CRT_DER_PATH`` and ``DEVICE_KEY_DER_PATH``.
    """
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
    """Assemble provisioning inputs for shared sample tests.

    ``wifi`` may be ``None`` when ``pouch_provision_wifi`` is ``False``.
    """
    return ProvisioningCreds(certs=device_cert_bundle, wifi=wifi_creds)
