# Copyright (c) 2026 Neutron Emulation, LLC. MIT licensed.
"""MesenBackend against a fake mesen-gdb speaking the JSON line protocol.

The fake is a Python script standing in for the binary, so the client's
error handling - an error reply, an emulator that stops answering, one that
exits - is tested without an emulator or a ROM.
"""

import stat
import sys
import textwrap

import pytest

from mesen_gdb.rom_driver import mesen
from mesen_gdb.rom_driver.backend import RomDriverError, RomTimeout
from mesen_gdb.rom_driver.mesen import MesenBackend

FAKE = textwrap.dedent('''\
    #!{python}
    import json, sys, time
    def reply(**kw):
        sys.stdout.write(json.dumps(dict(ok=True, **kw)) + "\\n"); sys.stdout.flush()
    reply(stopped="ready", frame=0, pc=0x8000)
    mem = {{}}
    for line in sys.stdin:
        cmd, *args = line.split()
        if cmd == "frames":
            if args[0] == "999":
                time.sleep(60)                      # hangs
            reply(stopped="step", frame=int(args[0]), pc=0x8001, frames=int(args[0]))
        elif cmd == "until":
            reply(stopped="step", frame=10, pc=0x8001, frames=10, hit=False)
        elif cmd == "write":
            a = int(args[0], 16)
            for i, b in enumerate(bytes.fromhex(args[1])):
                mem[a + i] = b
            reply(bytes=len(args[1]) // 2)
        elif cmd == "mem":
            a, n = int(args[0], 16), int(args[1])
            reply(addr=a, hex=bytes(mem.get(a + i, 0) for i in range(n)).hex())
        elif cmd == "input":
            if args[0] == "1" and args[1] in ("l,right", "none"):
                reply()
            else:
                sys.stdout.write(json.dumps(dict(ok=False, error="input: " + " ".join(args))) + "\\n"); sys.stdout.flush()
        elif cmd == "emutimeout":
            sys.stdout.write(json.dumps(dict(ok=False, error="timeout")) + "\\n"); sys.stdout.flush()
        elif cmd == "die":
            sys.exit(3)
        elif cmd == "quit":
            break
        else:
            sys.stdout.write(json.dumps(dict(ok=False, error="unknown command: " + cmd)) + "\\n")
            sys.stdout.flush()
''')


@pytest.fixture
def backend(tmp_path, monkeypatch):
    fake = tmp_path / 'mesen-gdb'
    fake.write_text(FAKE.format(python=sys.executable))
    fake.chmod(fake.stat().st_mode | stat.S_IEXEC)
    monkeypatch.setattr(mesen, 'BASE_TIMEOUT', 2.0)
    b = MesenBackend(binary=str(fake))
    start = b.start(tmp_path / 'game.smc')
    assert start.reason == 'ready'
    yield b
    b.close()


def test_frames_and_memory(backend):
    stop = backend.frames(5)
    assert (stop.reason, stop.frame, stop.frames) == ('step', 5, 5)
    backend.write(0x7E0010, b'\x34\x12')
    assert backend.read(0x7E0010, 2) == b'\x34\x12'


def test_input_command(backend):
    backend.set_input(1, frozenset({'right', 'l'}))     # sorted, comma-joined
    backend.set_input(1, frozenset())                   # released: "none"
    with pytest.raises(RomDriverError, match='input: 2 a'):
        backend.set_input(2, frozenset({'a'}))


def test_until_reports_miss(backend):
    assert backend.until(0x8123, 10).hit is False


def test_error_reply_raises(backend):
    with pytest.raises(RomDriverError, match='unknown command'):
        backend._call('bogus')
    assert backend.frames(1).frame == 1          # still usable after an error


def test_emulator_timeout_reply_is_rom_timeout(backend):
    with pytest.raises(RomTimeout):
        backend._call('emutimeout')


def test_no_reply_kills_and_raises(backend, monkeypatch):
    monkeypatch.setattr(mesen, 'SECONDS_PER_FRAME', 0.0)
    proc = backend._proc
    with pytest.raises(RomTimeout, match='killed'):
        backend.frames(999)                      # the fake sleeps; deadline 2 s
    assert proc.poll() is not None               # the process is gone, not leaked


def test_exit_raises(backend):
    with pytest.raises(RomDriverError, match='exited'):
        backend._call('die')


def test_path_with_space_is_refused(backend, tmp_path):
    with pytest.raises(RomDriverError, match='whitespace'):
        backend.save_state(tmp_path / 'a b.mss')


def test_find_binary_from_env(tmp_path, monkeypatch):
    fake = tmp_path / 'mg'
    fake.write_text('')
    monkeypatch.setenv('MESEN_GDB', str(fake))
    assert mesen.find_binary() == fake
