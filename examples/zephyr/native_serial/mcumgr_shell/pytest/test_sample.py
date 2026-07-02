#
# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#


def test_mcumgr_shell(dut, server_process):
    """Verify that the client can execute shell commands on the remote server."""
    dut.readlines_until(regex="SHELL OK:", timeout=30)
    dut.readlines_until(regex="REMOTE UPTIME:", timeout=10)
    dut.readlines_until(regex="LOCAL UPTIME:", timeout=10)
