# ROM testing with `mesen_gdb.rom_driver`

`mesen_gdb.rom_driver` runs a built SNES ROM under an emulator from ordinary Python
tests (pytest or unittest). A test drives the controller with chainable calls
that run as they are made, reads memory by symbol name between them, and can
start at a named point in the game instead of power-on. The emulator is a
`Backend`; Mesen-GDB is the one implemented.

## Install

    pip install -e Mesen-GDB/python          # add [test] for pytest and pytest-xdist

Installing it registers the pytest fixtures (a `pytest11` entry point). Its
own tests run with `pytest Mesen-GDB/python/tests`. This package is MIT
licensed; the emulator around it is GPLv3.

## Quick start

```python
from mesen_gdb.rom_driver import Rom

rom = Rom("build/GAME.smc")             # GAME.sym beside it, and GAME.dbg or main.dbg
with rom.session() as s:                # powered on, paused, pad released
    s.press("x", frames=30)             # held from the very first frame
    s.wait(20).right(30)                # runs 50 frames, then returns s
    assert s.var.camera_x == 0x5A       # memory by name, at this frame
    s.start().until("main_loop")        # tap Start, run to a label
    s.screenshot("shot.png")
```

With pytest, a project's `tests/conftest.py` supplies the ROM and imports the
fixtures:

```python
import pytest
from pathlib import Path
from mesen_gdb.rom_driver import Rom
from mesen_gdb.rom_driver.pytest_plugin import snes, state   # noqa: F401

@pytest.fixture(scope="session")
def rom():
    return Rom(Path(__file__).parent.parent / "build" / "GAME.smc")
```

Its states are builders in a module the tests import, `tests/states.py`:

```python
from mesen_gdb.rom_driver import rom_state

@rom_state
def level1(s):
    s.wait(60).start().wait(120)        # through the title into level 1
```

```python
from states import level1

def test_jump(snes):                    # a session at power-on
    snes.wait(60).start().wait(120).a()
    assert snes.var.player_y < 0xA0

def test_boss_door(state):              # a session restored to `level1`
    s = state(level1)
    s.right(400).upright(20)
    assert s.var.room == 3
```

Run with `pytest tests`, or `pytest tests -n auto` (pytest-xdist): every session
is its own emulator process. R65's `rotor_raiders/tests/` is a full working suite.

## Input

Every input method runs the emulator and returns the session, so calls chain.

| Call | Does |
|---|---|
| `wait(n)` | n frames, every button released |
| `run(n)` | n frames, buttons as they are |
| `a() b() x() y() l() r() start() select()` | tap: `tap_down` frames down (3), `tap_up` up (3) - one press for the game |
| `up(n) down(n) left(n) right(n)` | hold n frames, then release (no `n`: a tap) |
| `upleft(n) upright(n) downleft(n) downright(n)` | the diagonals, the same way |
| `a(n)`, `start(n)`, ... | any button held n frames |
| `press("l", "a", frames=3)` / `hold(...)` | several buttons together (no `frames`: a tap) |
| `until(target, max_frames=600, holding=())` | run to a symbol or address, holding buttons meanwhile; `NotReached` if it is not hit in time |
| `jump(target)` | set the PC to a symbol or address without running. Stopped at a function's entry, a jump to another function of the same signature swaps the call: `s.until("boot_mission", 2).jump("boot_harness")` |

A session starts powered on, paused before the first frame, with power-on RAM
pinned (zeros), so a run repeats exactly. Input is per frame, which is how the
SNES polls; `s.port = 2` drives the second controller. `s.frame` is the frame
counter as of the last call; `s.last` the full stop (`pc`, registers).

## Motions: input as objects

For exact, overlapping or reusable input - a fighting game's special move, a
charge, a combo - write a **motion**: a string of single-character ops,
parsed once into an immutable `Motion` that `s.play(...)` runs.

| Letters | Buttons | | Letters | Buttons |
|---|---|---|---|---|
| `U D L R` | up down left right | | `Q E` | L / R shoulders |
| `A B X Y` | face buttons | | `S T` | start / select |

Uppercase presses a button, lowercase releases it, `.` (or `_`) releases
everything held, and a number advances that many frames with the pad as it
stands. Whitespace is ignored.

```python
from mesen_gdb.rom_driver import motion, pad

hadouken = motion("D5R5dX5xr")     # down 5; down+right 5; right+X 5; release
s.play(hadouken)                   # or s.play("D5 R5 d X5 .") - the same
s.play(hadouken.mirrored())        # facing left: L and R swapped

charge = motion("L60")             # a motion may end with buttons held ...
boom = motion("lR2X5xr")           # ... they stay held into the next one
s.play(charge).play(boom)

s.play(motion("A2a2") * 5)         # repeat; `+` concatenates
s.play("D").a().play("d")          # motions and the chain methods compose
```

Pressing a button that is already held, or releasing one that is not, is a
`ValueError`: a typo fails loudly. `s.held` is what the current port holds.
The chain methods are additive - `s.a()` taps A without letting go of a
motion's Down - while `wait(n)` releases everything.

**Several controllers at once**: a `pad` runs one motion per port (from 1) on
one frame clock; a shorter one's port keeps what it ended with while the rest
finish. Pads concatenate into phases played one after another.

```python
s.play(pad({1: motion("U50."), 2: motion("D23")}))
s.play(pad({1: "R30r"}) + pad({1: "A2a", 2: "A2a"}))
```

A bare motion plays on `s.port` (1 by default).

Every run is bounded in emulated frames, and every call has a wall-clock
deadline as a backstop: a call that overruns kills the emulator and raises
`RomTimeout`. A test can fail; it cannot hang.

## States

A state is a builder - a function that drives a session to a point in the game -
marked with `@rom_state`, optionally starting where another ends, and named by
the function itself:

```python
@rom_state
def level1(s):
    s.wait(60).start().wait(120)

@rom_state(parent=level1)
def level1_boss(s):
    s.right(2400).wait(60)
```

`state(level1_boss)` (the fixture) or `rom.session(state=level1_boss)`
opens a session restored to it. A function not marked with `@rom_state` is
refused. The first use builds the chain - `level1` from
power-on, then `level1_boss` from there - and saves each as a save state in
`build/.rom_driver/`, keyed by the ROM's hash and every builder's source, so
editing a builder (or a parent) rebuilds it and a rebuilt ROM does too. A helper
a builder calls is not part of the key: delete the cache after changing one. A
state captures the machine, not the pad: a session from one starts with every
button released. `rom.session(state=Path("x.mss"))` loads a save state file
directly - one made by hand in the debugger, say.

## Memory by name

Symbols come from the assembler's `.sym` (WLA-DX: the truth for code labels,
ROM data and `.RAMSECTION` statics, sized by any `_sizeof_<label>`) and a
cc65-format `.dbg` (every symbol it lists with its size, including RAM named by
`.DEFINE`, and constants). A name both know takes the `.sym` address, since a
compiler may write the `.dbg` before assembly. `Rom` finds `GAME.sym` beside the
ROM, and `GAME.dbg` or `main.dbg`; `Rom(path, sym=..., dbg=...)` names them.

Names are exactly as the files spell them, and the files carry sizes, not
types:

| Expression | Reads |
|---|---|
| `s.var.camera_x` | a 1-3 byte symbol, little-endian, unsigned |
| `s.var.UNIT_MAX` | a constant's value |
| `s.var.units__x_px.u16[3]` | element 3 of a larger symbol, at the width named |
| `s.var.units__hp.u8[3]`, `s.raw("buf").bytes()` | the same; `raw` for any size |
| `s.u8(addr)`, `s.u16("name")`, `s.read(t, n)`, `s.write(t, v, size)` | explicit |

Assignments write: `s.var.funds = 9`, `s.var.units__hp.u8[3] = 1`. A symbol
of more than 3 bytes is not assignable whole.

Also: `s.save_state()`, `s.load_state(path)`, `s.screenshot(path)`,
`s.dump("oam")` (bytes of a memory region).

## Backends

`Rom(path, backend=...)` takes an emulator's name or a callable returning a
`Backend` (`mesen_gdb/rom_driver/backend.py`): `start`, `set_input(port, buttons)`,
`frames`, `until`, `jump`, `read`, `write`, `save_state`, `load_state`, `screenshot`,
`dump`, `close`. The unit tests drive the API through an in-memory one
(`tests/fake_backend.py`).

**Mesen-GDB** (`"mesen"`, the default): one `mesen-gdb ROM --headless --json
--ram-init zeros` process per session, speaking a line protocol (one command
per line, one JSON reply each; Mesen-GDB/CLAUDE.md, "JSON protocol"), with its
`input` command for the pad. The binary is the `binary=` backend argument, else
`$MESEN_GDB`, else the repo build at `bin/linux-x64/Release/mesen-gdb`, else
`mesen-gdb` on `PATH`. It emulates 300-400 frames a second
headless; named states and parallel workers are what make a suite fast.
