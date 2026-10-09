# Copyright (c) 2026 Neutron Emulation, LLC. MIT licensed.
"""The chainable input API -> the backend calls it makes."""

import pytest

from mesen_gdb.rom_driver import NotReached, Rom
from mesen_gdb.rom_driver.symbols import Symbol, Symbols

from .fake_backend import RecordingBackend

PORT = 1


@pytest.fixture
def s(tmp_path):
    rom_file = tmp_path / 'game.smc'
    rom_file.write_bytes(b'\0' * 1024)
    rom = Rom(rom_file, backend=RecordingBackend, cache_dir=tmp_path / 'cache')
    rom.symbols = Symbols({'main_loop': Symbol('main_loop', 0x8123, None, 'label', 'sym')})
    session = rom.session()
    session.backend.calls.clear()
    yield session
    session.close()


def calls(s):
    return [c for c in s.backend.calls if c[0] in ('input', 'frames', 'until')]


def inp(*buttons):
    return ('input', PORT, frozenset(buttons))


def test_hold_then_release(s):
    s.left(20)
    assert calls(s) == [inp('left'), ('frames', 20), inp()]


def test_tap_is_down_then_up(s):
    s.start()
    assert calls(s) == [inp('start'), ('frames', 3), inp(), ('frames', 3)]


def test_diagonals_and_combinations(s):
    s.downleft(50).press('l', 'right', frames=3)
    assert calls(s) == [inp('down', 'left'), ('frames', 50), inp(),
                        inp('l', 'right'), ('frames', 3), inp()]


def test_chain_and_wait_do_not_resend_the_same_pad(s):
    result = s.wait(200).left(20).downleft(50)
    assert result is s
    assert calls(s) == [('frames', 200), inp('left'), ('frames', 20), inp(),
                        inp('down', 'left'), ('frames', 50), inp()]
    assert s.frame == 270


def test_run_keeps_the_pad(s):
    s.run(5)
    assert calls(s) == [('frames', 5)]


def test_tap_lengths_are_settable(s):
    s.tap_down, s.tap_up = 1, 2
    s.a()
    assert calls(s) == [inp('a'), ('frames', 1), inp(), ('frames', 2)]


def test_second_controller(s):
    s.port = 2
    s.b(4)
    assert calls(s)[0] == ('input', 2, frozenset({'b'}))


def test_unknown_button(s):
    with pytest.raises(ValueError, match="'q'"):
        s.press('q', frames=1)


def test_until_hit_holding_and_release(s):
    s.backend.hit_addr = 0x8123
    s.until('main_loop', holding=('select',))
    assert calls(s) == [inp('select'), ('until', 0x8123, 600), inp()]


def test_jump(s):
    s.backend.hit_addr = 0x8123
    s.until('main_loop').jump(0x9000)
    assert [c for c in s.backend.calls if c[0] in ('until', 'jump')] == [('until', 0x8123, 600), ('jump', 0x9000)]
    assert s.last.pc == 0x9000


def test_until_miss(s):
    with pytest.raises(NotReached, match=r'main_loop \$008123 not reached in 30 frames'):
        s.until('main_loop', max_frames=30)
