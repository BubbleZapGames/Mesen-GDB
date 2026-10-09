# Copyright (c) 2026 Neutron Emulation, LLC. MIT licensed.
"""`s.var` access by name, against a fake backend's memory."""

from types import SimpleNamespace

import pytest

from mesen_gdb.rom_driver.session import Session
from mesen_gdb.rom_driver.symbols import Symbol, Symbols

from .fake_backend import RecordingBackend


def sym(name, addr, size, kind='label'):
    return name, Symbol(name, addr, size, kind, 'dbg')


@pytest.fixture
def s():
    # Arrays of 4 (a u16 one and a u8 one, as a compiler might name the
    # planes of a struct-of-arrays pool), and a few plain statics.
    symbols = Symbols(dict([
        sym('units__x_px', 0x100, 8),
        sym('units__hp', 0x108, 4),
        sym('heli_x_px', 0x200, 2),
        sym('mode', 0x202, 1),
        sym('buf', 0x210, 4),
        sym('UNIT_MAX', 32, None, 'const'),
    ]))
    rom = SimpleNamespace(symbols=symbols)
    session = Session(rom, RecordingBackend())
    yield session
    session.close()


def test_scalars_and_constants(s):
    s.backend.write(0x200, b'\x34\x12')
    assert s.var.heli_x_px == 0x1234
    assert s.var.UNIT_MAX == 32
    s.var.mode = 7
    assert s.backend.mem[0x202] == 7


def test_arrays_read_at_the_width_named(s):
    s.backend.write(0x100 + 2 * 2, b'\xEF\xBE')     # units__x_px[2]
    s.backend.write(0x108 + 2, b'\x05')             # units__hp[2]
    assert s.var.units__x_px.u16[2] == 0xBEEF
    assert s.var.units__hp.u8[2] == 5
    s.var.units__hp.u8[3] = 9
    assert s.backend.mem[0x108 + 3] == 9


def test_array_is_not_assignable_whole(s):
    with pytest.raises(AttributeError, match='write an element'):
        s.var.units__hp = 1


def test_raw_arrays(s):
    s.backend.write(0x210, b'\x01\x02\x03\x04')
    assert s.raw('buf').u8[2] == 3
    assert s.raw('buf').u16[1] == 0x0403
    assert s.var.buf.bytes() == b'\x01\x02\x03\x04'     # 4 bytes: not a scalar


def test_unknown_name(s):
    with pytest.raises(KeyError, match='heli_x_px'):
        s.var.heli_x
