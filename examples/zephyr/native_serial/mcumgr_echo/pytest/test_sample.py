#
# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#


def test_mcumgr_echo(dut, server_process):
    """Verify that the client can send an MCUmgr OS echo command to the server."""
    dut.readlines_until(regex="ECHO OK: test123", timeout=30)
