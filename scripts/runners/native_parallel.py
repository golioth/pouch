# Copyright (c) 2026 Golioth, Inc.
# SPDX-License-Identifier: Apache-2.0

"""West runner that launches multiple native_sim executables in parallel,
with optional socat-based virtual serial links between them.

Serial links are declared at the sysbuild level via the CMake helper
``native_parallel_serial_link()`` (see pouch/cmake/native_parallel.cmake),
which writes a ``native_parallel.yaml`` config file into the sysbuild
build directory.  This runner reads that file at ``west flash`` time to
create socat virtual null-modem pairs and wire the correct UART ports
into each process's command line.
"""

import argparse
import os
import pty
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from contextlib import contextmanager
from functools import cached_property
from pathlib import Path

import yaml

from domains import Domain, Domains

from runners.core import RunnerCaps, RunnerConfig, ZephyrBinaryRunner
from signal_utils import interruptible_sigterm

# ANSI color codes for domain prefixes (green, blue, yellow, cyan, magenta, red)
_DOMAIN_COLORS = [32, 34, 33, 36, 35, 31]


class NativeParallelBinaryRunner(ZephyrBinaryRunner):
    """Runs native_sim executables in parallel with optional serial links."""

    def __init__(
        self,
        cfg,
        domains_selected=None,
        foreground_domain=None,
        serial_exes=None,
        output_mode=None,
        tui=False,
        gdb_args=None,
    ):
        super().__init__(cfg)

        if self.cfg.exe_file is None:
            raise ValueError(
                "The provided RunnerConfig is missing the required field 'exe_file'."
            )

        self._domains_selected = domains_selected
        self._foreground_domain = foreground_domain
        self.serial_exes = serial_exes or {}
        self.output_mode = output_mode or "prefixed"
        self.tui = tui
        self.gdb_args = gdb_args or []

        if cfg.gdb is None:
            self.gdb_cmd = None
        else:
            self.gdb_cmd = [cfg.gdb] + (["-tui"] if tui else [])

    @classmethod
    def name(cls):
        return "native_parallel"

    @classmethod
    def capabilities(cls):
        return RunnerCaps(commands={"flash", "debug"})

    @classmethod
    def do_add_parser(cls, parser: argparse.ArgumentParser):
        parser.add_argument(
            "--foreground-domain",
            help="Domain to run in the foreground for 'west flash' or to "
            "launch under GDB for 'west debug' "
            "(default: last domain in flash order)",
        )
        parser.add_argument(
            "--output",
            choices=["prefixed", "passthrough"],
            default="prefixed",
            dest="output_mode",
            help="Output mode: 'prefixed' adds colored [DOMAIN] prefix to "
            "each line (default), 'passthrough' preserves raw output",
        )
        parser.add_argument(
            "--tui",
            default=False,
            action="store_true",
            help="if given, GDB uses -tui",
        )
        parser.add_argument(
            "--gdb-args",
            action="append",
            help="pass additional arguments to GDB",
        )

    @classmethod
    def args_from_previous_runner(cls, previous_runner, args):
        if not hasattr(args, "serial_exes"):
            args.serial_exes = {}
        args.serial_exes.update(previous_runner.serial_exes)
        # Propagate runner options across domains so CLI flags work with
        # multi-domain sysbuild (west passes runner args only to the
        # first domain).
        if hasattr(previous_runner, "output_mode"):
            args.output_mode = previous_runner.output_mode
        if hasattr(previous_runner, "tui"):
            args.tui = previous_runner.tui
        if hasattr(previous_runner, "gdb_args"):
            args.gdb_args = previous_runner.gdb_args

    @classmethod
    def do_create(
        cls, cfg: RunnerConfig, args: argparse.Namespace
    ) -> "NativeParallelBinaryRunner":
        return cls(
            cfg,
            domains_selected=args.domain,
            foreground_domain=getattr(args, "foreground_domain", None),
            serial_exes=getattr(args, "serial_exes", None),
            output_mode=getattr(args, "output_mode", None),
            tui=getattr(args, "tui", False),
            gdb_args=getattr(args, "gdb_args", None),
        )

    def do_run(self, command: str, **kwargs):
        # West invokes the runner once per domain in flash_order.  Each
        # invocation records its exe in self.serial_exes (shared via
        # args_from_previous_runner) and returns early; the final
        # invocation sees a complete map and launches all processes.
        self.serial_exes[self.domain.name] = self.cfg.exe_file

        if command == "flash":
            self.do_flash(**kwargs)
        elif command == "debug":
            self.do_debug(**kwargs)
        else:
            raise AssertionError(f"Unsupported command: {command}")

    @cached_property
    def domains_all(self) -> list[Domain]:
        domains_file = Path(self.sysbuild_conf.build_dir) / "domains.yaml"
        domains_from_file = Domains.from_file(domains_file)
        return domains_from_file.get_domains(default_flash_order=True)

    @cached_property
    def domains_selected(self) -> list[str]:
        sel = self._domains_selected
        if not sel:
            sel = [d.name for d in self.domains_all]
        return sel

    @cached_property
    def domain(self) -> Domain:
        return [
            d for d in self.domains_all if d.build_dir == self.build_conf.build_dir
        ][0]

    @cached_property
    def foreground_domain(self) -> str:
        fg = self._foreground_domain
        if not fg:
            fg = self.domains_selected[-1]
        return fg

    @cached_property
    def link_config(self) -> list[dict]:
        """Load serial link definitions from native_parallel.yaml."""
        config_file = Path(self.sysbuild_conf.build_dir) / "native_parallel.yaml"
        if not config_file.exists():
            return []
        with open(config_file) as f:
            cfg = yaml.safe_load(f)
        return cfg.get("links", []) if cfg else []

    @contextmanager
    def socat_links(self, tmpdir: Path):
        """Create socat virtual null-modem pairs for every configured link.

        Yields a dict mapping ``(domain, uart)`` tuples to their assigned
        PTY symlink paths.
        """
        port_map: dict[tuple[str, str], Path] = {}
        socat_procs: list[subprocess.Popen] = []

        all_ports: list[Path] = []

        for i, link in enumerate(self.link_config):
            port_a = tmpdir / f"link_{i}_a"
            port_b = tmpdir / f"link_{i}_b"

            proc = subprocess.Popen(
                [
                    "socat",
                    f"PTY,link={port_a},raw,echo=0",
                    f"PTY,link={port_b},raw,echo=0",
                ],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            socat_procs.append(proc)
            all_ports.extend([port_a, port_b])

            a = link["a"]
            b = link["b"]
            port_map[(a["domain"], a["uart"])] = port_a
            port_map[(b["domain"], b["uart"])] = port_b

        # Wait for all symlinks to appear
        if all_ports:
            deadline = time.time() + 5
            while not all(p.exists() for p in all_ports):
                time.sleep(0.05)
                if time.time() > deadline:
                    for p in socat_procs:
                        p.terminate()
                        p.wait()
                    raise TimeoutError("socat PTY pair creation timed out")

            self.logger.info(f"socat links ready: {list(port_map.values())}")

        try:
            yield port_map
        finally:
            for p in socat_procs:
                p.terminate()
            for p in socat_procs:
                p.wait()

    def _build_cmds(self, port_map):
        """Build per-domain native_sim command lines with UART port flags."""
        cmds: dict[str, list[str]] = {}
        for dname, exe in self.serial_exes.items():
            cmd = [exe]
            if dname == self.foreground_domain:
                cmd.append("-uart_stdinout")
            for (link_domain, uart), port in port_map.items():
                if link_domain == dname:
                    cmd.append(f"-{uart}_port={port}")
            cmds[dname] = cmd
        return cmds

    def do_flash(self, **kwargs):
        # Non-sysbuild single-image build: nothing to coordinate.
        if not self.sysbuild_conf.options:
            self.check_call([self.cfg.exe_file])
            return

        # Defer until called for the last domain (see do_run).
        if self.domain.name != self.domains_selected[-1]:
            return

        tmpdir = Path(tempfile.mkdtemp(prefix="zephyr_parallel_"))
        try:
            with interruptible_sigterm(), self.socat_links(tmpdir) as port_map:
                cmds = self._build_cmds(port_map)
                if self.output_mode == "prefixed":
                    self._launch_prefixed(cmds)
                else:
                    self._launch_passthrough(cmds)
        finally:
            shutil.rmtree(tmpdir, ignore_errors=True)

    def do_debug(self, **kwargs):
        if self.gdb_cmd is None:
            raise ValueError(
                "GDB not available; no 'gdb' was configured for this runner."
            )

        # Non-sysbuild single-image build: just run gdb on the exe.
        if not self.sysbuild_conf.options:
            cmd = self.gdb_cmd + self.gdb_args + ["--quiet", self.cfg.exe_file]
            self.check_call(cmd)
            return

        # Defer until called for the last domain (see do_run).
        if self.domain.name != self.domains_selected[-1]:
            return

        # No interruptible_sigterm() anywhere in the debug path: GDB
        # puts the terminal in raw mode, and a SystemExit raised mid
        # session would leave it broken.  Background socat / domain
        # processes are cleaned up by _launch_debug's finally block.
        tmpdir = Path(tempfile.mkdtemp(prefix="zephyr_parallel_"))
        try:
            with self.socat_links(tmpdir) as port_map:
                cmds = self._build_cmds(port_map)
                self._launch_debug(cmds)
        finally:
            shutil.rmtree(tmpdir, ignore_errors=True)

    @staticmethod
    def _prefix_reader(master_fd: int, tag: str):
        """Read lines from a PTY master and print with a [DOMAIN] prefix.

        Runs in a dedicated thread.  Uses raw os.read() to handle the
        EIO that occurs when the slave end closes (normal PTY teardown).
        """
        buf = b""
        try:
            while True:
                data = os.read(master_fd, 4096)
                if not data:
                    break
                buf += data
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    text = line.decode("utf-8", errors="replace")
                    sys.stdout.write(tag + text + "\n")
                    sys.stdout.flush()
        except OSError:
            pass  # OSError (EIO) is the normal signal that the slave end closed.
        finally:
            if buf:
                text = buf.decode("utf-8", errors="replace")
                sys.stdout.write(tag + text + "\n")
                sys.stdout.flush()
            os.close(master_fd)

    def _build_prefix_tag(self, dname, idx, max_len, use_color):
        """Build the '[domain] ' tag used in 'prefixed' output mode."""
        bracket = f"[{dname}]".rjust(max_len + 2)
        if not use_color:
            return bracket + " "
        color = _DOMAIN_COLORS[idx % len(_DOMAIN_COLORS)]
        return f"\033[{color}m{bracket}\033[0m "

    def _open_prefixed_pty(self, tag):
        """Create a PTY pair with a daemon reader that prefixes output.

        Returns (slave_fd, thread).  The caller passes slave_fd to its
        subprocess as stdout/stderr and is responsible for closing it.
        """
        master_fd, slave_fd = pty.openpty()
        t = threading.Thread(
            target=self._prefix_reader, args=(master_fd, tag), daemon=True
        )
        t.start()
        return slave_fd, t

    @contextmanager
    def _prefixed_outputs(self, domain_names):
        """Allocate a PTY + reader-thread for each domain.

        Yields a {name: slave_fd} mapping.  The caller passes each
        slave_fd to its subprocess and is responsible for closing it
        after the subprocess is launched.  On exit, joins reader
        threads with a short timeout to drain output.
        """
        use_color = sys.stdout.isatty()
        max_len = max(len(n) for n in domain_names)
        slaves: dict[str, int] = {}
        threads: list[threading.Thread] = []
        for i, dname in enumerate(domain_names):
            tag = self._build_prefix_tag(dname, i, max_len, use_color)
            slave_fd, t = self._open_prefixed_pty(tag)
            slaves[dname] = slave_fd
            threads.append(t)
        try:
            yield slaves
        finally:
            for t in threads:
                t.join(timeout=2)

    def _start_background(self, dname, cmd, stdout_fd=None):
        """Launch a single background domain process."""
        self.logger.info(f"launching background: {dname}")
        if stdout_fd is None:
            return self.popen_ignore_int(cmd)
        proc = self.popen_ignore_int(cmd, stdout=stdout_fd, stderr=stdout_fd)
        os.close(stdout_fd)
        return proc

    def _start_backgrounds(self, cmds: dict[str, list[str]]):
        """Start all non-foreground domains per the configured output_mode.

        Returns (bg_procs, threads).  ``threads`` is empty when
        output_mode is 'passthrough'.
        """
        bg_names = [n for n in cmds if n != self.foreground_domain]
        if not bg_names:
            return [], []
        if self.output_mode != "prefixed":
            return [self._start_background(n, cmds[n]) for n in bg_names], []

        use_color = sys.stdout.isatty()
        max_len = max(len(n) for n in cmds)
        domain_order = list(cmds.keys())
        procs, threads = [], []
        for dname in bg_names:
            idx = domain_order.index(dname)
            tag = self._build_prefix_tag(dname, idx, max_len, use_color)
            slave_fd, t = self._open_prefixed_pty(tag)
            threads.append(t)
            procs.append(self._start_background(dname, cmds[dname], slave_fd))
        return procs, threads

    @staticmethod
    def _terminate_all(procs):
        """Terminate the given processes and wait for each to exit."""
        for p in procs:
            p.terminate()
        for p in procs:
            p.wait()

    def _launch_passthrough(self, cmds: dict[str, list[str]]):
        """Launch domains with raw, unprefixed stdout."""
        bg_procs = [
            self._start_background(n, c)
            for n, c in cmds.items()
            if n != self.foreground_domain
        ]

        fg_cmd = cmds[self.foreground_domain]
        self.logger.info(f"launching foreground: {self.foreground_domain}")
        # Plain Popen (no popen_ignore_int / no setsid): the foreground
        # process inherits the controlling terminal's process group so
        # interactive stdin and Ctrl+C work normally.  Prefixed mode
        # uses setsid because it captures output via a PTY instead.
        self._log_cmd(fg_cmd)
        fg_proc = subprocess.Popen(fg_cmd)

        try:
            rc = fg_proc.wait()
        finally:
            self._terminate_all([fg_proc, *bg_procs])

        if rc:
            raise subprocess.CalledProcessError(rc, fg_cmd)

    def _launch_prefixed(self, cmds: dict[str, list[str]]):
        """Launch all domains with PTY-captured, prefixed output."""
        with self._prefixed_outputs(list(cmds.keys())) as slaves:
            bg_procs: list[subprocess.Popen] = []
            fg_proc = None
            try:
                for dname, cmd in cmds.items():
                    slave_fd = slaves[dname]
                    if dname == self.foreground_domain:
                        self.logger.info(f"launching foreground: {dname}")
                        # popen_ignore_int uses setsid so the fg process
                        # won't receive the terminal's SIGINT (we
                        # terminate it explicitly in the finally block).
                        fg_proc = self.popen_ignore_int(
                            cmd, stdout=slave_fd, stderr=slave_fd
                        )
                        os.close(slave_fd)
                    else:
                        bg_procs.append(self._start_background(dname, cmd, slave_fd))
                rc = fg_proc.wait()
            finally:
                self._terminate_all(([fg_proc] if fg_proc else []) + bg_procs)

        if rc:
            raise subprocess.CalledProcessError(rc, cmds[self.foreground_domain])

    def _launch_debug(self, cmds: dict[str, list[str]]):
        """Run background domains per output_mode; run the foreground domain under GDB on the controlling terminal."""
        bg_procs, threads = self._start_backgrounds(cmds)

        fg_cmd = cmds[self.foreground_domain]
        gdb_full = self.gdb_cmd + self.gdb_args + ["--quiet", "--args"] + fg_cmd
        self.logger.info(f"launching foreground under GDB: {self.foreground_domain}")
        self._log_cmd(gdb_full)
        # Plain Popen: GDB owns the controlling terminal's stdin/stdout.
        # See do_debug() for why no interruptible_sigterm wraps this.
        fg_proc = subprocess.Popen(gdb_full)

        try:
            rc = fg_proc.wait()
        finally:
            self._terminate_all([fg_proc, *bg_procs])

        for t in threads:
            t.join(timeout=2)

        if rc:
            raise subprocess.CalledProcessError(rc, gdb_full)
