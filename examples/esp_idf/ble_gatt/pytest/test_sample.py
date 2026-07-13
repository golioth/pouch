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

from pytest_pouch.esp_idf_sample_tests import *  # noqa: F403


@pytest.fixture(scope="module")
def pouch_test_provision_wifi() -> bool:
    return False
