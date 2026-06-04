#
# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

import sys
from pathlib import Path

import pytest

sys.path.insert(
    0, str(Path(__file__).resolve().parents[4] / "scripts" / "pytest-pouch")
)

pytest_plugins = ["pytest_pouch.plugin", "pytest_pouch.esp_idf_harness"]


@pytest.fixture(scope="module")
def pouch_device_name_prefix() -> str:
    return "esp-idf-http-client"


@pytest.fixture(scope="module")
def pouch_provision_wifi() -> bool:
    return True
