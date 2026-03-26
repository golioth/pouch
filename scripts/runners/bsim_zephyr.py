# Copyright (c) 2025 Golioth, Inc.
# SPDX-License-Identifier: Apache-2.0

"""This file provides a ZephyrBinaryRunner that enables
flashing (running) a BabbleSim Zephyr application."""

from .bsim_base import BsimBinaryRunnerBase

class BsimZephyrBinaryRunner(BsimBinaryRunnerBase):
    """Runs the BabbleSim Zephyr application."""

    @classmethod
    def name(cls):
        return 'bsim_zephyr'

    @classmethod
    def args_from_previous_runner(cls, previous_runner, args):
        super().args_from_previous_runner(previous_runner, args)

        # Propagate the chosen device number (incremented by 1) to next runner
        if args.bsim_dev is None:
            args.bsim_dev = previous_runner.bsim_dev + 1
