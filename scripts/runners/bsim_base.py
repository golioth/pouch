# Copyright (c) 2025 Golioth, Inc.
# SPDX-License-Identifier: Apache-2.0

"""This file provides a ZephyrBinaryRunner that enables
flashing (running) a BabbleSim application."""

import argparse
from contextlib import contextmanager
from functools import cached_property
from pathlib import Path
import random
import string

from runners.core import RunnerCaps, RunnerConfig, ZephyrBinaryRunner

from domains import Domains, Domain

DEFAULT_GDB_PORT = 3333

class BsimBinaryRunnerBase(ZephyrBinaryRunner):
    """Runs the BabbleSim binary."""

    def __init__(self, cfg,
                 domains_selected=None,
                 foreground_domain=None,
                 bsim_id=None,
                 bsim_dev=None,
                 bsim_sim_length=None,
                 bsim_args=None,
                 bsim_cmds=None,
                 tui=False,
                 gdb_port=DEFAULT_GDB_PORT,
                 gdb_args=None):
        super().__init__(cfg)

        self.gdb_port = gdb_port
        self.gdb_args = gdb_args or []

        if cfg.gdb is None:
            self.gdb_cmd = None
        else:
            self.gdb_cmd = [cfg.gdb] + (['-tui'] if tui else [])

        if self.cfg.exe_file is None:
            raise ValueError("The provided RunnerConfig is missing the required field 'exe_file'.")

        self._domains_selected = domains_selected
        self._foreground_domain = foreground_domain
        self.bsim_id = bsim_id
        self.bsim_dev = bsim_dev
        self.bsim_sim_length = bsim_sim_length
        self.bsim_args = bsim_args or []
        self.bsim_cmds = bsim_cmds or {}

    @classmethod
    def capabilities(cls):
        return RunnerCaps(commands={'debug', 'flash'})

    @classmethod
    def do_add_parser(cls, parser: argparse.ArgumentParser):
        parser.add_argument('--bsim-id',
                            help='''string which uniquely identifies the
                            simulation''')
        parser.add_argument('--bsim-dev', type=int,
                            help='''device number''')
        parser.add_argument('--bsim-arg', action='append',
                            help='''if given, add arguments to zephyr.exe
                            invocation''')
        parser.add_argument('--bsim-sim-length',
                            help='''Length of the simulation, in us (only
                            applies to 2G4_phy)''')
        parser.add_argument('--foreground-domain',
                            help='''Domain name of the program to run/debug
                            in the foreground''')
        parser.add_argument('--tui', default=False, action='store_true',
                            help='if given, GDB uses -tui')
        parser.add_argument('--gdb-port', default=DEFAULT_GDB_PORT,
                            help=f'gdb port, defaults to {DEFAULT_GDB_PORT}')
        parser.add_argument('--gdb-args', action="append",
                            help='pass additional arguments to GDB')

    @classmethod
    def args_from_previous_runner(cls, previous_runner, args):
        if args.bsim_id is None:
            args.bsim_id = previous_runner.bsim_id

        if not hasattr(args, 'bsim_cmds'):
            args.bsim_cmds = {}
        args.bsim_cmds.update(previous_runner.bsim_cmds)

    @classmethod
    def do_create(cls, cfg: RunnerConfig, args: argparse.Namespace) -> ZephyrBinaryRunner:
        return cls(cfg,
                   domains_selected=args.domain,
                   foreground_domain=args.foreground_domain,
                   bsim_id=args.bsim_id,
                   bsim_dev=args.bsim_dev,
                   bsim_sim_length=args.bsim_sim_length,
                   bsim_args=args.bsim_arg,
                   bsim_cmds=getattr(args, 'bsim_cmds', None),
                   tui=args.tui,
                   gdb_port=args.gdb_port,
                   gdb_args=args.gdb_args)

    def do_run(self, command: str, **kwargs):
        if not self.bsim_id:
            self.bsim_id = ''.join(random.choice(string.ascii_letters) for _ in range(16))

        self.bsim_cmds[self.domain.name] = self.exec_cmd()

        if command == 'flash':
            self.do_flash(**kwargs)
        elif command == 'debug':
            self.do_debug(**kwargs)
        else:
            raise AssertionError

    def exec_cmd(self):
        if not self.bsim_dev:
            self.bsim_dev = 0

        bsim_cmd = [
            self.cfg.exe_file,
            f'-s={self.bsim_id}',
            f'-d={self.bsim_dev}',
        ] + self.bsim_args

        return bsim_cmd, {
            'cwd': self.build_conf.build_dir,
        }

    @cached_property
    def domains_all(self) -> list[Domain]:
        domains_file = Path(self.sysbuild_conf.build_dir) / 'domains.yaml'

        domains_from_file = Domains.from_file(domains_file)

        return domains_from_file.get_domains(default_flash_order=True)

    @cached_property
    def domains_selected(self) -> list[str]:
        domains_selected = self._domains_selected
        if not domains_selected:
            domains_selected = [d.name for d in self.domains_all]

        return domains_selected

    @cached_property
    def domain(self) -> Domain:
        return [d for d in self.domains_all
                if d.build_dir == self.build_conf.build_dir][0]

    @cached_property
    def foreground_domain(self) -> str:
        foreground_domain = self._foreground_domain
        if not foreground_domain:
            foreground_domain = self.domains_selected[-1]

        return foreground_domain

    def is_default_domain(self) -> bool:
        return self.domains.get_default_domain().build_dir \
            == self.build_conf.build_dir

    @contextmanager
    def run_background_domains(self):
        # This is the last domain, so start all launch all processes now.
        procs = []

        for name, (args, kwargs) in self.bsim_cmds.items():
            # Run all background domains
            if self.foreground_domain != name:
                procs.append(self.popen_ignore_int(args, **kwargs))
        try:
            yield procs
        finally:
            # Terminate all previous domain processes
            for proc in procs:
                proc.terminate()
            for proc in procs:
                proc.wait()

    def do_flash(self, **kwargs):
        if not self.sysbuild_conf.options:
            cmd = [self.cfg.exe_file]
            self.check_call(cmd)
            return

        if self.domain.name != self.domains_selected[-1]:
            # Just run for the last domain
            return

        # This is the last domain, so launch all processes now.

        with self.run_background_domains():
            # Run foreground domain
            args, kwargs = self.bsim_cmds[self.foreground_domain]
            self.check_call(args, **kwargs)

    def do_debug(self, **kwargs):
        if not self.sysbuild_conf.options:
            cmd = (self.gdb_cmd + self.gdb_args + ['--quiet', self.cfg.exe_file])
            self.check_call(cmd)
            return

        if self.domain.name != self.domains_selected[-1]:
            # Just run for the last domain
            return

        # This is the last domain, so start all launch all processes now.

        with self.run_background_domains():
            # Run foreground domain
            args, kwargs = self.bsim_cmds[self.foreground_domain]
            self.check_call(self.gdb_cmd + self.gdb_args + ['--quiet', '--args'] + args, **kwargs)
