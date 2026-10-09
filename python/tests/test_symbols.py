# Copyright (c) 2026 Neutron Emulation, LLC. MIT licensed.
"""Symbol loading from .sym and .dbg files."""

import pytest

from mesen_gdb.rom_driver.symbols import Symbols

# Shapes taken from a real build (rotor_raiders): a code label the .dbg has
# stale, a ROM table under its own label with a _sizeof_, a RAM static in a
# .RAMSECTION, and .DEFINE'd lowram statics only the .dbg knows.
SYM = """\
[information]
[labels]
00:f4d8 snapshot_point
02:9a54 Console__init
02:b61b __mission_names_data
[definitions]
000000a0 _sizeof___mission_names_data
00000011 _sizeof_screen_con
[ramsections]
7e:2080 screen_con
"""

DBG = """\
version\tmajor=2,minor=0
sym\tid=1,name="snapshot_point",addrsize=absolute,val=0x00F4BE,seg=0,type=lab
sym\tid=2,name="mission_names",addrsize=absolute,val=0x000000,size=160,type=lab
sym\tid=3,name="units__x_px",addrsize=absolute,val=0x0017B0,size=64,type=lab
sym\tid=4,name="units__kind__tag",addrsize=absolute,val=0x0018B0,size=32,type=lab
sym\tid=5,name="units__kind__data",addrsize=absolute,val=0x0018D0,size=128,type=lab
sym\tid=6,name="units__kind__Infantry__pose",addrsize=absolute,val=0x0018F0,size=32,type=lab
sym\tid=7,name="heli_x_px",addrsize=absolute,val=0x001624,size=2,type=lab
sym\tid=8,name="UNIT_MAX",addrsize=absolute,val=0x000020,type=equ
"""


@pytest.fixture
def syms(tmp_path):
    (tmp_path / 'g.sym').write_text(SYM)
    (tmp_path / 'g.dbg').write_text(DBG)
    return Symbols.load(sym=tmp_path / 'g.sym', dbg=tmp_path / 'g.dbg')


def test_sym_wins_over_stale_dbg_code_address(syms):
    assert syms.addr('snapshot_point') == 0x00F4D8
    assert syms['snapshot_point'].source == 'sym'


def test_rom_table_by_its_label_with_size(syms):
    t = syms['__mission_names_data']
    assert (t.addr, t.size) == (0x02B61B, 160)
    assert 'mission_names' not in syms              # the .dbg's unplaced 0 is dropped


def test_ramsection_static_has_size(syms):
    assert (syms.addr('screen_con'), syms['screen_con'].size) == (0x7E2080, 0x11)


def test_define_statics_and_constants_from_dbg(syms):
    assert syms['heli_x_px'].size == 2
    assert syms['UNIT_MAX'].kind == 'const' and syms.addr('UNIT_MAX') == 32


def test_missing_name_suggests(syms):
    with pytest.raises(KeyError, match='units__x_px'):
        syms['x_px']
