#
# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

import sys
from pathlib import Path

sys.path.insert(
    0, str(Path(__file__).resolve().parents[4] / "scripts" / "pytest-pouch")
)

pytest_plugins = [
    "pytest_pouch.plugin",
    "pytest_pouch.ota_harness",
]
