#
# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

"""The 400 KiB dummy OTA checks BLE integrity, not deterministic backpressure."""

import pytest
from pytest_pouch.zephyr_ota_tests import test_ota_sha256  # noqa: F401

pytestmark = pytest.mark.anyio
