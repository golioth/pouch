#
# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#


def test_unity_host_results(dut):
    # This will boot the Linux app and wait for Unity output
    dut.expect_unity_test_output()
