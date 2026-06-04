#
# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

"""Compatibility shim for pytest-embedded with esptool 4.x.

Why this exists:
- Current pytest-embedded-idf emits hyphenated command names such as
  ``write-flash`` and ``erase-flash``.
- Our ESP-IDF environment pins ``esptool==4.11.0`` (required by our current
  IDF toolchain), which expects underscore command names like
  ``write_flash`` and ``erase_flash``.

What this does:
- Patches ``esptool.main`` at import time.
- Only when esptool major version is 4, translates the known hyphenated
  command tokens to their underscore equivalents.

Removal guidance:
- This shim should be removable once the project moves to an ESP-IDF/toolchain
  combination that supports esptool 5.x command naming end-to-end.
"""

from __future__ import annotations

import esptool

_ORIGINAL_ESPTOOL_MAIN = esptool.main

_V5_TO_V4_COMMAND_MAP = {
    "erase-flash": "erase_flash",
    "erase-region": "erase_region",
    "read-flash": "read_flash",
    "verify-flash": "verify_flash",
    "write-flash": "write_flash",
}


def _esptool_major_version() -> int:
    version = getattr(esptool, "__version__", "")
    try:
        return int(str(version).split(".", maxsplit=1)[0])
    except (TypeError, ValueError):
        return 0


def _patched_esptool_main(argv, *args, **kwargs):
    if isinstance(argv, list) and _esptool_major_version() == 4:
        argv = [_V5_TO_V4_COMMAND_MAP.get(token, token) for token in argv]

    return _ORIGINAL_ESPTOOL_MAIN(argv, *args, **kwargs)


esptool.main = _patched_esptool_main
