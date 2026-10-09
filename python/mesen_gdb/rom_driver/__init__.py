# Copyright (c) 2026 Neutron Emulation, LLC. MIT licensed.
"""mesen_gdb.rom_driver - SNES ROM tests as ordinary Python tests.

A `Rom` is a built ROM with its symbols; a `Session` is one emulator with
it loaded. Input is chainable and live - ``s.wait(200).left(20).downleft(50)``
runs as it is called, and exact or overlapping input is a `motion` object
(``s.play(motion("D5R5dX5xr"))``), several controllers at once a `pad`.
Memory is read by symbol name (``s.var.camera_x``, ``s.var.units__x_px.u16[3]``),
and a named state (`rom_state`) puts a test at a point in the game without
replaying the way there. The emulator is a pluggable `Backend`; Mesen-GDB is
the one there is.

For pytest, `mesen_gdb.rom_driver.pytest_plugin` provides `snes` (a session at
power-on) and `state` (``state("level1")``: a session restored to a named
state), given a `rom` fixture. See python/README.md in Mesen-GDB.
"""

from .backend import BUTTONS, Backend, NotReached, RomDriverError, RomTimeout, Stop
from .motion import Motion, Pad, motion, pad
from .session import Rom, Session
from .states import rom_state
from .symbols import Symbol, Symbols

__all__ = ['BUTTONS', 'Backend', 'Motion', 'NotReached', 'Pad', 'Rom', 'RomDriverError',
           'RomTimeout', 'Session', 'Stop', 'Symbol', 'Symbols', 'motion', 'pad',
           'rom_state']
