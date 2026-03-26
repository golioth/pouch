# Copyright (c) 2025 Golioth, Inc.
# SPDX-License-Identifier: Apache-2.0

"""This file provides a ZephyrBinaryRunner that enables
flashing (running) BabbleSim arbitrary application."""

from pathlib import Path

from .bsim_base import BsimBinaryRunnerBase

class BsimDeviceBinaryRunner(BsimBinaryRunnerBase):
    """Runs the BabbleSim arbitrary application."""

    @classmethod
    def name(cls):
        return 'bsim_device'

    @classmethod
    def args_from_previous_runner(cls, previous_runner, args):
        super().args_from_previous_runner(previous_runner, args)

        # Propagate the chosen device number (incremented by 1) to next runner
        if args.bsim_dev is None:
            args.bsim_dev = previous_runner.bsim_dev + 1

    def exec_cmd(self):
        bsim_cmd = [
            self.cfg.exe_file,
            f'-s={self.bsim_id}',
            f'-d={self.bsim_dev}',
        ] + self.bsim_args

        # Requires running from inside tools/bsim/bin directory in order to
        # properly access shared libraries, which are linked with relative path
        return bsim_cmd, {
            'cwd': Path(self.cfg.exe_file).parent,
        }
