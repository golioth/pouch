# Copyright (c) 2025 Golioth, Inc.
# SPDX-License-Identifier: Apache-2.0

"""This file provides a ZephyrBinaryRunner that enables
flashing (running) a BabbleSim 2G4 Phy application."""

from pathlib import Path

from .bsim_base import BsimBinaryRunnerBase

class BsimPhyBinaryRunner(BsimBinaryRunnerBase):
    """Runs the BabbleSim 2G4 Phy binary."""

    @classmethod
    def name(cls):
        return 'bsim_phy'

    @classmethod
    def args_from_previous_runner(cls, previous_runner, args):
        super().args_from_previous_runner(previous_runner, args)

        if args.bsim_dev is None:
            args.bsim_dev = previous_runner.bsim_dev

    def exec_cmd(self):
        if not self.bsim_dev:
            self.bsim_dev = -1

        bsim_cmd = [
            self.cfg.exe_file,
            f'-s={self.bsim_id}',
        ] + self.bsim_args

        if self.bsim_sim_length is not None:
            bsim_cmd += [
                f'-sim_length={self.bsim_sim_length}',
            ]

        bsim_cmd += [
            f'-D={len(self.domains_all) - 1}',
        ]

        # Requires running from inside tools/bsim/bin directory in order to
        # properly access shared libraries, which are linked with relative path
        return bsim_cmd, {
            'cwd': Path(self.cfg.exe_file).parent,
        }
