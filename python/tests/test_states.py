# Copyright (c) 2026 Neutron Emulation, LLC. MIT licensed.
"""Named states: registration, parent chains, building once, caching."""

import pytest

from mesen_gdb.rom_driver import Rom, rom_state
from mesen_gdb.rom_driver import states

from .fake_backend import RecordingBackend

built = []


# Names are prefixed: the registry is global to the test process.
@rom_state(name='t_level1')
def level1(s):
    built.append('level1')
    s.wait(100)
    s.write(0x10, 1)


@rom_state(name='t_level1_boss', parent='t_level1')
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
    assert states.lookup('t_level1').build is level1


def test_chain_is_root_first():
    assert [d.name for d in states.chain('t_level1_boss')] == ['t_level1', 't_level1_boss']


def test_session_from_a_state(rom):
    with rom.session(state='t_level1_boss') as s:
        assert s.frame == 150                       # 100 + the 50 of the child
        assert s.u8(0x10) == 1 and s.u8(0x11) == 2
    assert built == ['level1', 'boss']


def test_built_once_then_cached(rom):
    first = rom.state('t_level1_boss')
    second = rom.state('t_level1_boss')
    assert first == second and first.is_file()
    assert built == ['level1', 'boss']              # not rebuilt
    with rom.session(state='t_level1') as s:        # the parent was cached on the way
        assert s.frame == 100
    assert built == ['level1', 'boss']


def test_key_follows_the_rom(rom, tmp_path):
    a = rom.state('t_level1')
    other_file = tmp_path / 'other.smc'
    other_file.write_bytes(b'\1' * 1024)
    other = Rom(other_file, backend=RecordingBackend, cache_dir=tmp_path / 'cache')
    assert other.state('t_level1') != a


def test_unknown_state(rom):
    with pytest.raises(KeyError, match="no state 'nope'"):
        rom.session(state='nope')


def test_cycle_is_an_error():
    rom_state(name='t_a', parent='t_b')(lambda s: None)
    rom_state(name='t_b', parent='t_a')(lambda s: None)
    with pytest.raises(ValueError, match='own ancestor'):
        states.chain('t_a')


def test_session_from_a_file(rom):
    path = rom.state('t_level1')
    with rom.session(state=path) as s:
        assert s.frame == 100
