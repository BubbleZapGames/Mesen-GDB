# Copyright (c) 2026 Neutron Emulation, LLC. MIT licensed.
"""Controller input as objects: motions, and pads of them for several controllers.

A motion is a string of single-character ops:

    U D L R    up down left right       Q E    the L / R shoulder buttons
    A B X Y    the face buttons          S T    start / select

Uppercase presses a button, lowercase releases it, ``.`` (or ``_``) releases
everything held, and a decimal number advances that many frames with the pad
as it stands. Whitespace is ignored. So a hadouken facing right is

    hadouken = motion("D5R5dX5xr")    # or "D5 R5 d X5 ." - the same thing

- Down, 5 frames; Down+Right, 5; Right+X, 5; then nothing held. A change
takes effect at the next advance; changes left at the end apply when the
motion finishes. A motion may end with buttons held - ``motion("L60")``
charges back - and they stay held into whatever is played next.

Motions combine: ``a + b``, ``m * 3``, ``m.mirrored()`` (left and right
swapped - the other facing). `pad` runs one motion per controller on one
frame clock, ports numbered from 1:

    s.play(pad({1: motion("U50."), 2: motion("D23")}))

Each port's motion starts on the same frame; a shorter one's port keeps what
it ended with until the longest finishes.
"""

from dataclasses import dataclass
from typing import Dict, FrozenSet, Iterable, List, Mapping, Tuple, Union

# Notation letter -> button name (backend.BUTTONS).
LETTERS = {'U': 'up', 'D': 'down', 'L': 'left', 'R': 'right',
           'A': 'a', 'B': 'b', 'X': 'x', 'Y': 'y',
           'Q': 'l', 'E': 'r', 'S': 'start', 'T': 'select'}
_LETTER_OF = {b: k for k, b in LETTERS.items()}
_MIRROR = {'left': 'right', 'right': 'left'}
RELEASE_ALL = '._'

Op = Tuple    # ('press', b) | ('release', b) | ('release_all',) | ('advance', n)
Buttons = FrozenSet[str]


def _normalize(ops: Iterable[Op]) -> Tuple[Op, ...]:
    """Merge adjacent advances and drop empty ones, so equal motions compare equal."""
    out: List[Op] = []
    for op in ops:
        if op[0] == 'advance':
            if op[1] == 0:
                continue
            if out and out[-1][0] == 'advance':
                out[-1] = ('advance', out[-1][1] + op[1])
                continue
        out.append(op)
    return tuple(out)


@dataclass(frozen=True)
class Motion:
    """One controller's input over time. Build with `motion`."""
    ops: Tuple[Op, ...] = ()

    def __post_init__(self):
        object.__setattr__(self, 'ops', _normalize(self.ops))

    @property
    def frames(self) -> int:
        """Frames the motion advances in all."""
        return sum(op[1] for op in self.ops if op[0] == 'advance')

    def __add__(self, other: 'Motion') -> 'Motion':
        return Motion(self.ops + _as_motion(other).ops)

    def __mul__(self, n: int) -> 'Motion':
        return Motion(self.ops * n)

    __rmul__ = __mul__

    def mirrored(self) -> 'Motion':
        """Left and right swapped: the same motion facing the other way."""
        return Motion(tuple((op[0], _MIRROR.get(op[1], op[1])) if op[0] in ('press', 'release') else op
                            for op in self.ops))

    def __str__(self) -> str:
        out = []
        for op in self.ops:
            if op[0] == 'press':
                out.append(_LETTER_OF[op[1]])
            elif op[0] == 'release':
                out.append(_LETTER_OF[op[1]].lower())
            elif op[0] == 'release_all':
                out.append('.')
            else:
                out.append(str(op[1]))
        return ''.join(out)

    def __repr__(self) -> str:
        return f'motion({str(self)!r})'

    def changes(self, start: Buttons) -> Tuple[List[Tuple[int, Buttons]], int]:
        """The held buttons over time, from *start*: ``([(frame, held)...], length)``.

        Each entry holds from its frame until the next. Pressing a button
        already held, or releasing one that is not, is a `ValueError` - the
        sign of a typo.
        """
        held, t = frozenset(start), 0
        out: List[Tuple[int, Buttons]] = [(0, held)]
        for i, op in enumerate(self.ops):
            kind = op[0]
            if kind == 'advance':
                t += op[1]
                continue
            if kind == 'press':
                if op[1] in held:
                    raise ValueError(f"{self!r}: op {i} presses {op[1]}, which is already held")
                held = held | {op[1]}
            elif kind == 'release':
                if op[1] not in held:
                    raise ValueError(f"{self!r}: op {i} releases {op[1]}, which is not held")
                held = held - {op[1]}
            else:
                held = frozenset()
            if out[-1][0] == t:
                out[-1] = (t, held)
            else:
                out.append((t, held))
        return out, t


def motion(text: str) -> Motion:
    """Parse the notation (module docstring) into a `Motion`."""
    ops: List[Op] = []
    i = 0
    while i < len(text):
        c = text[i]
        if c.isspace():
            i += 1
        elif c.isdigit():
            j = i
            while j < len(text) and text[j].isdigit():
                j += 1
            ops.append(('advance', int(text[i:j])))
            i = j
        elif c in RELEASE_ALL:
            ops.append(('release_all',))
            i += 1
        elif c.upper() in LETTERS:
            ops.append(('press' if c.isupper() else 'release', LETTERS[c.upper()]))
            i += 1
        else:
            raise ValueError(f"motion {text!r}: unknown {c!r} at {i} "
                             f"(letters: {' '.join(LETTERS)}, '.' or '_' to release all, numbers advance)")
    return Motion(tuple(ops))


def _as_motion(m: Union[Motion, str]) -> Motion:
    return m if isinstance(m, Motion) else motion(m)


@dataclass(frozen=True)
class Pad:
    """Motions for several controllers, in phases played one after another.

    Within a phase every port's motion starts on the same frame; the phase
    lasts as long as its longest motion. `+` appends phases.
    """
    phases: Tuple[Tuple[Tuple[int, Motion], ...], ...]

    def __add__(self, other: Union['Pad', Motion, str]) -> 'Pad':
        if not isinstance(other, Pad):
            raise TypeError("add a pad to a pad; a motion has no port of its own")
        return Pad(self.phases + other.phases)

    def __mul__(self, n: int) -> 'Pad':
        return Pad(self.phases * n)

    __rmul__ = __mul__

    def mirrored(self) -> 'Pad':
        return Pad(tuple(tuple((p, m.mirrored()) for p, m in phase) for phase in self.phases))

    @property
    def frames(self) -> int:
        return sum(max((m.frames for _, m in phase), default=0) for phase in self.phases)

    def __repr__(self) -> str:
        return ' + '.join('pad({' + ', '.join(f'{p}: {m!r}' for p, m in phase) + '})'
                          for phase in self.phases)

    def timeline(self, start: Mapping[int, Buttons]) -> Tuple[List[Tuple[Dict[int, Buttons], int]], Dict[int, Buttons]]:
        """Merge the ports onto one clock.

        Returns ``(segments, end)``: each segment is ``({port: held}, frames)``
        - set those buttons, then advance - and *end* is every port's held
        buttons once the last phase finishes. Ports not in a phase keep what
        they hold.
        """
        held: Dict[int, Buttons] = {p: frozenset(b) for p, b in start.items()}
        segments: List[Tuple[Dict[int, Buttons], int]] = []
        for phase in self.phases:
            tracks = {}
            length = 0
            for port, m in phase:
                tracks[port], n = m.changes(held.get(port, frozenset()))
                length = max(length, n)
            bounds = sorted({t for track in tracks.values() for t, _ in track} | {0, length})
            for t0, t1 in zip(bounds, bounds[1:]):
                now = dict(held)
                for port, track in tracks.items():
                    now[port] = [b for t, b in track if t <= t0][-1]
                segments.append((now, t1 - t0))
            for port, track in tracks.items():
                held[port] = track[-1][1]
        return segments, held


def pad(mapping: Mapping[int, Union[Motion, str]]) -> Pad:
    """One motion per controller port (1 = the first), started together."""
    for port in mapping:
        if not isinstance(port, int) or port < 1:
            raise ValueError(f"pad: ports are numbered from 1, not {port!r}")
    return Pad((tuple((p, _as_motion(m)) for p, m in sorted(mapping.items())),))
