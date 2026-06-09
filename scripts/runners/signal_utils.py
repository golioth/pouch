# Copyright (c) 2026 Golioth, Inc.
# SPDX-License-Identifier: Apache-2.0

"""Signal handling utilities shared by multi-process west runners."""

import signal
from contextlib import contextmanager

__all__ = ["interruptible_sigterm"]


def _sigterm_handler(signum, _frame):
    """SIGTERM handler that raises SystemExit.

    Python's default SIGTERM handler (SIG_DFL) terminates the process
    at the kernel level, so any surrounding finally block never runs.
    A Python-level handler that returns normally does not help either:
    under PEP 475, the interrupted syscall (e.g. os.waitpid()) is
    silently retried on EINTR.  Raising an exception from the handler
    bypasses that retry and lets finally-block process cleanup run.
    """
    raise SystemExit(128 + signum)


@contextmanager
def interruptible_sigterm():
    """Context manager that installs a SIGTERM handler raising SystemExit.

    Use around code that blocks in os.waitpid() (via subprocess.Popen.wait(),
    check_call(), etc.) to ensure SIGTERM from ``timeout``, CI, or external
    signals interrupts the wait and lets finally-block cleanup run.
    """
    old = signal.signal(signal.SIGTERM, _sigterm_handler)
    try:
        yield
    finally:
        signal.signal(signal.SIGTERM, old)
