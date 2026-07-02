#
# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#


def test_serial_communication(dut, client_process):
    """Verify that the client can send data to the server over a serial link."""
    dut.readlines_until(regex="RECEIVED: Hello from client!", timeout=10)
