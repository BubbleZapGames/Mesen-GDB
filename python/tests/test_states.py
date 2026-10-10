# Copyright (c) 2026 Neutron Emulation, LLC. MIT licensed.
"""States: builders, parent chains, building once, caching."""

import pytest

from mesen_gdb.rom_driver import Rom, rom_state
from mesen_gdb.rom_driver import states

from .fake_backend import RecordingBackend

built = []


@rom_state
def level1(s):
    built.append('level1')
    s.wait(100)
    s.write(0x10, 1)


@rom_state(parent=level1)
def level1_boss(s):
    built.append('boss')
    s.right(50)
    s.write(0x11, 2)


@pytest.fixture
def rom(tmp_path):
    built.clear()
    rom_file = tmp_path / 'game.smc'
    rom_file.write_bytes(b'\0' * 1024)
    return Rom(rom_file, backend=RecordingBackend, cache_dir=tmp_path / 'cache')


def test_decorator_keeps_the_function():
    assert level1.__name__ == 'level1'
    assert states.definition(level1).build is level1


def test_chain_is_root_first():
    assert [d.name for d in states.chain(level1_boss)] == ['level1', 'level1_boss']


def test_session_from_a_state(rom):
    with rom.session(state=level1_boss) as s:
        assert s.frame == 150                       # 100 + the 50 of the child
        assert s.u8(0x10) == 1 and s.u8(0x11) == 2
    assert built == ['level1', 'boss']


def test_built_once_then_cached(rom):
    first = rom.state(level1_boss)
    second = rom.state(level1_boss)
    assert first == second and first.is_file()
    assert built == ['level1', 'boss']              # not rebuilt
    with rom.session(state=level1) as s:            # the parent was cached on the way
        assert s.frame == 100
    assert built == ['level1', 'boss']


def test_key_follows_the_rom(rom, tmp_path):
    a = rom.state(level1)
    other_file = tmp_path / 'other.smc'
    other_file.write_bytes(b'\1' * 1024)
    other = Rom(other_file, backend=RecordingBackend, cache_dir=tmp_path / 'cache')
    assert other.state(level1) != a


def test_unmarked_function_is_refused(rom):
    def nope(s):
        pass
    with pytest.raises(TypeError, match="nope is not a state"):
        rom.session(state=nope)
    with pytest.raises(TypeError, match="is not a state"):
        rom.session(state='level1')                 # a name is not a state


def test_cycle_is_an_error():
    def a(s):
        pass
    b = rom_state(parent=rom_state(a))(lambda s: None)
    rom_state(parent=b)(a)                          # re-marked: a now starts where b ends
    with pytest.raises(ValueError, match='own ancestor'):
        states.chain(a)


def test_session_from_a_file(rom):
    path = rom.state(level1)
    with rom.session(state=path) as s:
        assert s.frame == 100
