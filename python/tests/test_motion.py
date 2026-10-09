# Copyright (c) 2026 Neutron Emulation, LLC. MIT licensed.
"""Motions and pads: the notation, the combinators, and what `play` sends."""

import pytest

from mesen_gdb.rom_driver import Motion, Rom, motion, pad

from .fake_backend import RecordingBackend


@pytest.fixture
def s(tmp_path):
    rom_file = tmp_path / 'game.smc'
    rom_file.write_bytes(b'\0' * 1024)
    session = Rom(rom_file, backend=RecordingBackend, cache_dir=tmp_path / 'cache').session()
    session.backend.calls.clear()
    yield session
    session.close()


def calls(s):
    return [c for c in s.backend.calls if c[0] in ('input', 'frames', 'until')]


def inp(port, *buttons):
    return ('input', port, frozenset(buttons))


# ---- notation ---------------------------------------------------------------

def test_parse_hadouken():
    assert motion("D5R5dX5xr").ops == (
        ('press', 'down'), ('advance', 5), ('press', 'right'), ('advance', 5),
        ('release', 'down'), ('press', 'x'), ('advance', 5), ('release', 'x'), ('release', 'right'))


def test_letters():
    m = motion("UDLR ABXY QE ST")
    assert [op[1] for op in m.ops] == ['up', 'down', 'left', 'right', 'a', 'b', 'x', 'y',
                                       'l', 'r', 'start', 'select']


def test_numbers_whitespace_and_merged_advances():
    assert motion("A 120 a") == motion("A120a")
    assert motion("60 3").ops == (('advance', 63),)
    assert motion("A120a").frames == 120


def test_release_all_spellings():
    assert motion("D5R5dX5.") == motion("D5R5dX5_")
    assert motion(".").ops == (('release_all',),)


def test_unknown_letter():
    with pytest.raises(ValueError, match="unknown 'Z' at 2"):
        motion("D5Z")


def test_round_trip_through_str():
    for text in ("D5R5dX5xr", "Q3S3qs", "L60.", "UR2ur"):
        assert str(motion(text)) == text
        assert motion(str(motion(text))) == motion(text)


def test_combinators():
    a, b = motion("D5"), motion("R5d")
    assert a + b == motion("D5R5d")
    assert a + "R5d" == motion("D5R5d")
    assert motion("A2a2") * 3 == motion("A2a2A2a2A2a2")
    assert motion("D5R5dX5xr").mirrored() == motion("D5L5dX5xl")
    assert motion("D5R5dX5xr").mirrored().mirrored() == motion("D5R5dX5xr")
    assert repr(motion("D5")) == "motion('D5')"


def test_pad_ports_from_one():
    with pytest.raises(ValueError, match='numbered from 1'):
        pad({0: motion("U5")})


# ---- play ---------------------------------------------------------------------

def test_play_hadouken(s):
    s.play(motion("D5R5dX5xr"))
    assert calls(s) == [inp(1, 'down'), ('frames', 5), inp(1, 'down', 'right'), ('frames', 5),
                        inp(1, 'right', 'x'), ('frames', 5), inp(1)]
    assert s.held == frozenset()


def test_play_string_and_release_all(s):
    s.play("D5R5dX5.")
    assert calls(s)[-2:] == [('frames', 5), inp(1)]


def test_held_carries_into_the_next_motion(s):
    s.play(motion("L60"))                      # charge back ...
    assert s.held == {'left'}
    s.play(motion("lR2X5xr"))                  # ... and release into forward + punch
    assert calls(s) == [inp(1, 'left'), ('frames', 60), inp(1, 'right'), ('frames', 2),
                        inp(1, 'right', 'x'), ('frames', 5), inp(1)]


def test_release_all_clears_and_is_harmless_when_empty(s):
    s.play("A3").play(".")
    assert s.held == frozenset()
    s.backend.calls.clear()
    s.play(".")
    assert calls(s) == []


def test_press_held_and_release_unheld_are_errors(s):
    s.play("A3")
    with pytest.raises(ValueError, match='presses a, which is already held'):
        s.play("A3")
    with pytest.raises(ValueError, match='releases b, which is not held'):
        s.play("b")


def test_motions_compose_with_the_chain_methods(s):
    s.play("D")                                # crouch, held
    assert calls(s) == [inp(1, 'down')]
    s.backend.calls.clear()
    s.a()                                      # tap A while crouching
    assert calls(s) == [inp(1, 'down', 'a'), ('frames', 3), inp(1, 'down'), ('frames', 3)]
    s.backend.calls.clear()
    s.wait(1)                                  # wait releases everything
    assert calls(s) == [inp(1), ('frames', 1)]


def test_two_controllers(s):
    s.play(pad({1: motion("U50."), 2: motion("D23")}))
    # Port 2's motion ends at 23 changing nothing, so the 50 frames run in one go.
    assert calls(s) == [inp(1, 'up'), inp(2, 'down'), ('frames', 50), inp(1)]
    s.port = 2
    assert s.held == {'down'}                  # port 2 keeps what its motion left


def test_pad_phases_run_in_order(s):
    p = pad({1: "A2a"}) + pad({2: "B3b"})
    assert p.frames == 5
    s.play(p)
    assert calls(s) == [inp(1, 'a'), ('frames', 2), inp(1), inp(2, 'b'), ('frames', 3), inp(2)]


def test_a_motion_plays_on_the_current_port(s):
    s.port = 2
    s.play("Y4y")
    assert calls(s) == [inp(2, 'y'), ('frames', 4), inp(2)]


def test_motion_is_immutable_value():
    m = motion("A1")
    with pytest.raises(Exception):
        m.ops = ()
    assert isinstance(m, Motion) and hash(m) == hash(motion("A1"))
