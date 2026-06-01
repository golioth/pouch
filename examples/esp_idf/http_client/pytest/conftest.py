#
# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

from __future__ import annotations

import os
import base64

import pytest

# Patch esptool v4.* for underscore/dash conflict with v5.*
import esptool_v4_shim  # noqa: F401


def _device_name_from_cert_cn() -> str | None:
    cert_der_b64 = os.getenv("DEVICE_CRT_DER_B64")
    if not cert_der_b64:
        return None

    try:
        from cryptography import x509
        from cryptography.x509.oid import NameOID

        cert_der = base64.b64decode(cert_der_b64, validate=True)
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
def provisioning_creds() -> dict[str, str]:
    required_env_vars = (
        "WIFI_SSID",
        "WIFI_PSK",
        "DEVICE_CRT_DER_B64",
        "DEVICE_KEY_DER_B64",
    )

    missing = [name for name in required_env_vars if not os.getenv(name)]
    if missing:
        missing_str = ", ".join(missing)
        pytest.skip(f"Missing provisioning env vars: {missing_str}")

    return {name: os.environ[name] for name in required_env_vars}


@pytest.fixture(scope="module")
def cloud_config() -> dict[str, str | None]:
    required_env_vars = (
        "GOLIOTH_API_URL",
        "GOLIOTH_API_KEY",
    )

    missing = [name for name in required_env_vars if not os.getenv(name)]
    if missing:
        missing_str = ", ".join(missing)
        pytest.skip(f"Missing cloud env vars: {missing_str}")

    device_name = os.getenv("GOLIOTH_DEVICE_NAME")
    if not device_name:
        device_name = _device_name_from_cert_cn()

    if not device_name:
        pytest.skip(
            "Missing cloud device identity. Set GOLIOTH_DEVICE_NAME or provide "
            "DEVICE_CRT_DER_B64 with a certificate CN matching the cloud device name"
        )

    return {
        "api_url": os.environ["GOLIOTH_API_URL"],
        "api_key": os.environ["GOLIOTH_API_KEY"],
        "device_name": device_name,
        "project_id": os.getenv("GOLIOTH_PROJECT_ID"),
    }
