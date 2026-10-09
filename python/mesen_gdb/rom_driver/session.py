# Copyright (c) 2026 Neutron Emulation, LLC. MIT licensed.
"""`Rom` and `Session` - the test-facing API.

    rom = Rom("build/GAME.smc")              # .sym beside it, .dbg or main.dbg
    with rom.session() as s:                 # powered on, paused, pad released
        s.press("x", frames=30)              # held from the first frame
        s.wait(20).right(30).downleft(50)    # each call runs as it is made
        assert s.var.camera_x == 0x5A        # memory by name, between calls
        s.start().until("main_loop")         # tap Start, run to a label
        s.screenshot("shot.png")

Input methods run the emulator and return the session, so they chain. For
exact, overlapping or several-controller input, `play` takes a motion -
``s.play("D5R5dX5xr")`` - or a `pad` of them (motion.py). A
named state (`rom_state`, states.py) is a save state built once by such a
chain; ``rom.session(state="level1")`` starts there.
"""

import hashlib
import os
import shutil
import tempfile
from pathlib import Path
from typing import Dict, FrozenSet, Optional, Union

from . import states
from .backend import BUTTONS, Backend, NotReached, RomDriverError, Stop
from .motion import Motion, Pad, pad as _pad
from .symbols import Symbols

Target = Union[int, str]

TAP_DOWN = 3   # frames a tap holds a button: long enough for any game's poll ...
TAP_UP = 3     # ... and released for as long again, so it is exactly one press
UNTIL_FRAMES = 600

# The diagonals, as their two directions.
DIAGONALS = {'upleft': ('up', 'left'), 'upright': ('up', 'right'),
             'downleft': ('down', 'left'), 'downright': ('down', 'right')}


def _sha1(path: Path) -> str:
    return hashlib.sha1(path.read_bytes()).hexdigest().upper()


def _make_backend(name, **kwargs) -> Backend:
    if callable(name):                 # a Backend class or factory
        return name(**kwargs)
    if name == 'mesen':
        from .mesen import MesenBackend
        return MesenBackend(**kwargs)
    raise RomDriverError(f"unknown emulator backend {name!r} (have: mesen)")


class Rom:
    """A built ROM and its symbols.

    *backend* is an emulator's name (``"mesen"``) or a callable returning a
    `Backend`; *backend_args* go to it.
    """

    def __init__(self, path: Union[str, Path], *, sym: Optional[Path] = None, dbg: Optional[Path] = None,
                 backend: str = 'mesen', cache_dir: Optional[Path] = None, **backend_args):
        self.path = Path(path).resolve()
        if not self.path.is_file():
            raise RomDriverError(f"ROM not found: {self.path} (build it first)")
        sym = Path(sym) if sym else self.path.with_suffix('.sym')
        if dbg is None:
            for candidate in (self.path.with_suffix('.dbg'), self.path.parent / 'main.dbg'):
                if candidate.is_file():
                    dbg = candidate
                    break
        self.symbols = Symbols.load(sym=sym, dbg=dbg)
        self.sha1 = _sha1(self.path)
        self.backend = backend
        self.backend_args = backend_args
        self.cache_dir = Path(cache_dir) if cache_dir else self.path.parent / '.rom_driver'

    def session(self, state: Union[str, Path, None] = None) -> 'Session':
        """A started session: at power-on, or restored to *state*.

        *state* is a registered state's name (built if not cached) or a path
        to a save state file.
        """
        path = None
        if state is not None:
            path = Path(state) if isinstance(state, Path) else self.state(state)
        s = Session(self, _make_backend(self.backend, **self.backend_args))
        try:
            s._start()
            if path is not None:
                s.load_state(path)
        except BaseException:
            s.close()
            raise
        return s

    def state(self, name: str) -> Path:
        """The save state for the registered state *name*, built if not cached.

        Built from its parent's state (or power-on) by its builder, and keyed
        by this ROM's hash and every builder's source in the chain.
        """
        d = states.lookup(name)
        key = hashlib.sha1(f'{self.sha1}\n{states.fingerprint(name)}'.encode()).hexdigest()[:16]
        out = self.cache_dir / f'{name}-{key}.mss'
        if out.is_file():
            return out
        parent = self.state(d.parent) if d.parent else None
        self.cache_dir.mkdir(parents=True, exist_ok=True)
        with self.session(state=parent) as s:
            d.build(s)
            # Written beside, then renamed: parallel workers never read half a file.
            tmp = out.with_suffix(f'.{os.getpid()}.tmp')
            s.save_state(tmp)
            os.replace(tmp, out)
        return out


class Session:
    """One emulator with the ROM loaded, paused between calls.

    The input methods - `wait`, `press`, `hold`, one per button and one per
    direction (diagonals included) - run the emulator as they are called and
    return the session. A button method with no frame count taps (TAP_DOWN
    down, TAP_UP up); with one it holds that long, then releases.
    """

    def __init__(self, rom: Rom, backend: Backend, port: int = 1):
        self.rom = rom
        self.backend = backend
        self.port = port               # the controller the input methods drive
        self.tap_down = TAP_DOWN
        self.tap_up = TAP_UP
        self.last: Optional[Stop] = None
        self._held: Dict[int, FrozenSet[str]] = {}     # per port, as the backend has it
        self._tmp = Path(tempfile.mkdtemp(prefix='rom_driver-'))
        self.var = Vars(self)

    # ---- lifetime ---------------------------------------------------------

    def _start(self) -> Stop:
        self.last = self.backend.start(self.rom.path)
        return self.last

    def close(self) -> None:
        self.backend.close()
        shutil.rmtree(self._tmp, ignore_errors=True)

    def __enter__(self) -> 'Session':
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    @property
    def frame(self) -> int:
        """The emulator's frame counter as of the last call."""
        return self.last.frame if self.last else 0

    # ---- input ------------------------------------------------------------

    @property
    def held(self) -> FrozenSet[str]:
        """The buttons held on the current port."""
        return self._held.get(self.port, frozenset())

    def _set(self, buttons, port: Optional[int] = None) -> None:
        port = self.port if port is None else port
        buttons = frozenset(buttons)
        if buttons != self._held.get(port, frozenset()):
            self.backend.set_input(port, buttons)
            self._held[port] = buttons

    def _run(self, frames: int) -> None:
        if frames > 0:
            self.last = self.backend.frames(frames)

    @staticmethod
    def _buttons(names) -> FrozenSet[str]:
        out = set()
        for n in names:
            if n in DIAGONALS:
                out.update(DIAGONALS[n])
            elif n in BUTTONS:
                out.add(n)
            else:
                raise ValueError(f"unknown button {n!r}; buttons are "
                                 f"{', '.join(BUTTONS + tuple(DIAGONALS))}")
        return frozenset(out)

    def wait(self, frames: int) -> 'Session':
        """*frames* frames with every button released, on every port."""
        for port in list(self._held):
            self._set((), port)
        self._run(frames)
        return self

    def run(self, frames: int) -> 'Session':
        """*frames* frames with the buttons as they are."""
        self._run(frames)
        return self

    def press(self, *buttons: str, frames: Optional[int] = None) -> 'Session':
        """Hold *buttons* for *frames*, then release them; no count is a tap.

        Additive: buttons already held (by a motion, say) stay held.
        """
        pressed = self._buttons(buttons)
        self._set(self.held | pressed)
        self._run(self.tap_down if frames is None else frames)
        self._set(self.held - pressed)
        if frames is None:
            self._run(self.tap_up)
        return self

    def hold(self, *buttons: str, frames: int) -> 'Session':
        """`press` with the frame count required: *buttons* down *frames* frames."""
        return self.press(*buttons, frames=frames)

    def a(self, frames: Optional[int] = None) -> 'Session': return self.press('a', frames=frames)
    def b(self, frames: Optional[int] = None) -> 'Session': return self.press('b', frames=frames)
    def x(self, frames: Optional[int] = None) -> 'Session': return self.press('x', frames=frames)
    def y(self, frames: Optional[int] = None) -> 'Session': return self.press('y', frames=frames)
    def l(self, frames: Optional[int] = None) -> 'Session': return self.press('l', frames=frames)
    def r(self, frames: Optional[int] = None) -> 'Session': return self.press('r', frames=frames)
    def start(self, frames: Optional[int] = None) -> 'Session': return self.press('start', frames=frames)
    def select(self, frames: Optional[int] = None) -> 'Session': return self.press('select', frames=frames)
    def up(self, frames: Optional[int] = None) -> 'Session': return self.press('up', frames=frames)
    def down(self, frames: Optional[int] = None) -> 'Session': return self.press('down', frames=frames)
    def left(self, frames: Optional[int] = None) -> 'Session': return self.press('left', frames=frames)
    def right(self, frames: Optional[int] = None) -> 'Session': return self.press('right', frames=frames)
    def upleft(self, frames: Optional[int] = None) -> 'Session': return self.press('upleft', frames=frames)
    def upright(self, frames: Optional[int] = None) -> 'Session': return self.press('upright', frames=frames)
    def downleft(self, frames: Optional[int] = None) -> 'Session': return self.press('downleft', frames=frames)
    def downright(self, frames: Optional[int] = None) -> 'Session': return self.press('downright', frames=frames)

    def play(self, m: Union[Pad, Motion, str]) -> 'Session':
        """Run a motion on the current port, or a `pad` of them on theirs.

        Starts from what each port holds now, and leaves held whatever the
        motion leaves held.
        """
        if not isinstance(m, Pad):
            m = _pad({self.port: m})
        segments, end = m.timeline(self._held)
        for held, frames in segments:
            for port, buttons in held.items():
                self._set(buttons, port)
            self._run(frames)
        for port, buttons in end.items():
            self._set(buttons, port)
        return self

    def until(self, target: Target, max_frames: int = UNTIL_FRAMES, *, holding=()) -> 'Session':
        """Run to *target* (a symbol or address), *holding* buttons meanwhile.

        At most *max_frames* frames; `NotReached` if it is not hit. Buttons
        held are released once it stops.
        """
        addr = self.resolve(target)
        extra = self._buttons(holding) - self.held
        self._set(self.held | extra)
        self.last = self.backend.until(addr, max_frames)
        self._set(self.held - extra)
        if not self.last.hit:
            name = f"{target} " if isinstance(target, str) else ""
            raise NotReached(f"{name}${addr:06X} not reached in {max_frames} frames "
                             f"(stopped at ${self.last.pc:06X}, frame {self.last.frame})")
        return self

    def jump(self, target: Target) -> 'Session':
        """Set the PC to *target* (a symbol or address) without running.

        Stopped at a function's entry by `until`, a jump to another function
        of the same signature swaps the call: it runs in its place and
        returns to the same caller.
        """
        self.last = self.backend.jump(self.resolve(target))
        return self

    # ---- memory -----------------------------------------------------------

    def resolve(self, target: Target) -> int:
        return target if isinstance(target, int) else self.rom.symbols.addr(target)

    def read(self, target: Target, n: int) -> bytes:
        return self.backend.read(self.resolve(target), n)

    def write(self, target: Target, data: Union[bytes, int], size: int = 1) -> None:
        if isinstance(data, int):
            data = data.to_bytes(size, 'little')
        self.backend.write(self.resolve(target), data)

    def raw(self, name: str) -> 'Raw':
        """*name* as an array to index explicitly: ``s.raw("muni_up").u8[0]``.

        Use it where a static's size does not say its shape - a ``[u8; 2]``
        is two bytes, which ``s.var`` reads as one ``u16``.
        """
        sym = self.rom.symbols[name]
        return Raw(self, sym.addr, sym.size)

    def u8(self, target: Target) -> int:
        return self.read(target, 1)[0]

    def u16(self, target: Target) -> int:
        return int.from_bytes(self.read(target, 2), 'little')

    def u24(self, target: Target) -> int:
        return int.from_bytes(self.read(target, 3), 'little')

    # ---- state and output -------------------------------------------------

    def save_state(self, path: Optional[Path] = None) -> Path:
        path = Path(path) if path else self._tmp / 'state.mss'
        self.backend.save_state(path)
        return path

    def load_state(self, path: Path) -> Stop:
        self.last = self.backend.load_state(Path(path))
        return self.last

    def screenshot(self, path: Optional[Path] = None) -> Path:
        path = Path(path) if path else self._tmp / 'screen.png'
        self.backend.screenshot(path)
        return path

    def dump(self, region: str = 'wram', path: Optional[Path] = None) -> bytes:
        path = Path(path) if path else self._tmp / f'{region}.bin'
        self.backend.dump(region, path)
        return path.read_bytes()


# ---- access by name ----------------------------------------------------------
#
# The symbols carry addresses and sizes, not types: a symbol of 1-3 bytes reads
# as one little-endian unsigned value, and anything larger is `Raw`, read at a
# width the caller names.

def _le(b: bytes) -> int:
    return int.from_bytes(b, 'little')


class Vars:
    """``s.var.NAME``: a constant's value, a 1-3 byte symbol's value, or `Raw` bytes."""

    def __init__(self, session: Session):
        object.__setattr__(self, '_s', session)

    def __getattr__(self, name: str):
        s = self._s
        sym = s.rom.symbols[name]          # raises with suggestions
        if sym.kind == 'const':
            return sym.addr
        if sym.size in (1, 2, 3):
            return _le(s.read(sym.addr, sym.size))
        return Raw(s, sym.addr, sym.size)

    def __setattr__(self, name: str, value: int) -> None:
        sym = self._s.rom.symbols[name]
        if sym.size not in (1, 2, 3):
            raise AttributeError(f"{name} is {sym.size} bytes; write an element (.u8[i] = v) "
                                 f"or use session.write")
        self._s.write(sym.addr, value, sym.size)


class Raw:
    """An array or blob: pick a width explicitly - ``.u8[i]``, ``.u16[i]``."""

    def __init__(self, session: Session, addr: int, size: Optional[int]):
        self._s, self.addr, self.size = session, addr, size

    def bytes(self, n: Optional[int] = None) -> bytes:
        n = n if n is not None else self.size
        if n is None:
            raise RomDriverError(f"${self.addr:06X} has no known size; pass n")
        return self._s.read(self.addr, n)

    @property
    def u8(self):
        return _Indexed(self._s, self.addr, 1)

    @property
    def u16(self):
        return _Indexed(self._s, self.addr, 2)


class _Indexed:
    def __init__(self, session: Session, addr: int, width: int):
        self._s, self.addr, self.width = session, addr, width

    def __getitem__(self, i: int) -> int:
        return _le(self._s.read(self.addr + i * self.width, self.width))

    def __setitem__(self, i: int, value: int) -> None:
        self._s.write(self.addr + i * self.width, value, self.width)
