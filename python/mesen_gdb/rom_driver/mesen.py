# Copyright (c) 2026 Neutron Emulation, LLC. MIT licensed.
"""Mesen-GDB as a `Backend`: one ``mesen-gdb ROM --headless --json`` process.

Input goes through its ``input`` command (a live controller state, held
until changed) and power-on RAM is pinned to zeros, so a run repeats exactly.

The process speaks a line protocol (Mesen-GDB/CLAUDE.md, "JSON protocol"):
one command per stdin line, exactly one JSON reply line each. Every call
here has a wall-clock deadline as a backstop; the emulator bounds its own
runs in frames, and a call that still overruns kills the process and
raises `RomTimeout`, so a test can fail but never hang.
"""

import json
import os
import select
import shutil
import signal
import subprocess
import tempfile
import time
from pathlib import Path
from typing import List, Optional

from .backend import Backend, RomDriverError, RomTimeout, Stop

# The repo's own build, relative to this file: python/mesen_gdb/rom_driver -> Mesen-GDB.
_DEFAULT_BINARY = Path(__file__).resolve().parents[3] / 'bin' / 'linux-x64' / 'Release' / 'mesen-gdb'

# Wall-clock allowance per emulated frame, and for any command at all. The
# headless debugger runs ~400 frames a second; this is ten times slower.
SECONDS_PER_FRAME = 0.025
BASE_TIMEOUT = 15.0


def find_binary(explicit: Optional[str] = None) -> Path:
    """The mesen-gdb to run: *explicit*, $MESEN_GDB, the repo build, then $PATH."""
    for candidate in (explicit, os.environ.get('MESEN_GDB'), _DEFAULT_BINARY, shutil.which('mesen-gdb')):
        if candidate and Path(candidate).is_file():
            return Path(candidate)
    raise RomDriverError(f"mesen-gdb not found (set MESEN_GDB, build {_DEFAULT_BINARY}, or put it on PATH)")


class MesenBackend(Backend):
    def __init__(self, binary: Optional[str] = None, extra_args: Optional[List[str]] = None):
        self.binary = find_binary(binary)
        self.extra_args = list(extra_args or [])
        self._proc: Optional[subprocess.Popen] = None
        self._buf = b''
        self._log = None

    # ---- process ----------------------------------------------------------

    def start(self, rom: Path) -> Stop:
        # Emulator chatter goes to a file rather than a pipe nobody drains.
        self._log = tempfile.NamedTemporaryFile(prefix='mesen-', suffix='.log', delete=False)
        self._proc = subprocess.Popen(
            [str(self.binary), str(rom), '--headless', '--json', '--ram-init', 'zeros', *self.extra_args],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self._log,
            start_new_session=True)
        return self._stop(self._recv(BASE_TIMEOUT, 'startup'))

    def close(self) -> None:
        p, self._proc = self._proc, None
        if p is None:
            return
        try:
            if p.poll() is None:
                try:
                    p.stdin.write(b'quit\n')
                    p.stdin.flush()
                    p.stdin.close()
                except OSError:
                    pass
                try:
                    p.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    self._kill(p)
        finally:
            if self._log is not None:
                self._log.close()
                try:
                    os.unlink(self._log.name)
                except OSError:
                    pass
                self._log = None

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass

    @staticmethod
    def _kill(p: subprocess.Popen) -> None:
        try:
            os.killpg(p.pid, signal.SIGKILL)
        except (ProcessLookupError, PermissionError):
            p.kill()
        p.wait()

    def log_tail(self, n: int = 2000) -> str:
        if self._log is None:
            return ''
        try:
            with open(self._log.name, 'rb') as f:
                return f.read()[-n:].decode('utf-8', 'replace')
        except OSError:
            return ''

    # ---- protocol ---------------------------------------------------------

    def _recv(self, timeout: float, what: str) -> dict:
        p = self._proc
        if p is None:
            raise RomDriverError("emulator not running")
        deadline = time.monotonic() + timeout
        fd = p.stdout.fileno()
        while b'\n' not in self._buf:
            left = deadline - time.monotonic()
            if left <= 0:
                self._kill(p)
                self._proc = None
                raise RomTimeout(f"{what}: no reply within {timeout:.1f}s; emulator killed")
            ready, _, _ = select.select([fd], [], [], left)
            if not ready:
                continue
            chunk = os.read(fd, 65536)
            if not chunk:
                code = p.wait()
                self._proc = None
                raise RomDriverError(f"{what}: emulator exited ({code})\n{self.log_tail()}")
            self._buf += chunk
        line, self._buf = self._buf.split(b'\n', 1)
        try:
            return json.loads(line)
        except json.JSONDecodeError:
            raise RomDriverError(f"{what}: not a JSON reply: {line[:200]!r}") from None

    def _call(self, command: str, timeout: float = BASE_TIMEOUT) -> dict:
        p = self._proc
        if p is None:
            raise RomDriverError("emulator not running")
        try:
            p.stdin.write(command.encode() + b'\n')
            p.stdin.flush()
        except OSError as e:
            raise RomDriverError(f"{command}: emulator gone ({e})") from None
        reply = self._recv(timeout, command)
        if not reply.get('ok'):
            error = reply.get('error', 'unknown error')
            if error == 'timeout':
                raise RomTimeout(f"{command}: emulator gave up on its own allowance")
            raise RomDriverError(f"{command}: {error}")
        return reply

    @staticmethod
    def _stop(reply: dict) -> Stop:
        return Stop(reason=reply.get('stopped', ''), frame=reply.get('frame', 0),
                    pc=reply.get('pc', 0), frames=reply.get('frames'), hit=reply.get('hit'),
                    registers=reply.get('cpu'))

    @staticmethod
    def _path(path: Path) -> str:
        # The protocol splits a command on whitespace.
        s = str(path)
        if any(c.isspace() for c in s):
            raise RomDriverError(f"path contains whitespace, which mesen-gdb cannot take: {s!r}")
        return s

    @staticmethod
    def _run_timeout(frames: int) -> float:
        return BASE_TIMEOUT + frames * SECONDS_PER_FRAME

    # ---- Backend ----------------------------------------------------------

    def set_input(self, port: int, buttons) -> None:
        self._call(f"input {port} {','.join(sorted(buttons)) if buttons else 'none'}")

    def frames(self, n: int) -> Stop:
        return self._stop(self._call(f'frames {n}', self._run_timeout(n)))

    def until(self, addr: int, max_frames: int) -> Stop:
        return self._stop(self._call(f'until {addr:06X} {max_frames}', self._run_timeout(max_frames)))

    def jump(self, addr: int) -> Stop:
        # bank:addr, so the bank is set rather than kept from the current PC.
        return self._stop(self._call(f'jump {addr >> 16:02X}:{addr & 0xFFFF:04X}'))

    def read(self, addr: int, n: int) -> bytes:
        if n <= 0:
            return b''
        return bytes.fromhex(self._call(f'mem {addr:06X} {n}')['hex'])

    def write(self, addr: int, data: bytes) -> None:
        if data:
            self._call(f'write {addr:06X} {bytes(data).hex()}')

    def save_state(self, path: Path) -> None:
        self._call(f'savestate {self._path(path)}')

    def load_state(self, path: Path) -> Stop:
        return self._stop(self._call(f'loadstate {self._path(path)}'))

    def screenshot(self, path: Path) -> None:
        self._call(f'screenshot {self._path(path)}')

    def dump(self, region: str, path: Path) -> None:
        self._call(f'dump {region} {self._path(path)}')
