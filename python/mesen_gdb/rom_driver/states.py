# Copyright (c) 2026 Neutron Emulation, LLC. MIT licensed.
"""Game states: a test that starts at "level 1" rather than power-on.

A state is a builder - a function that drives a `Session` to the point the
state stands for - marked with `rom_state`, and referred to by the function
itself:

    from mesen_gdb.rom_driver import rom_state

    @rom_state
    def boarded(s):
        s.press("l", frames=30).press("l", "right", frames=3).wait(3) ...

    @rom_state(parent=boarded)            # starts where `boarded` ends
    def aloft(s):
        s.up(20).right(40)

`Rom.state(aloft)` builds the chain once, saves the machine as a save
state and caches it beside the ROM; `Rom.session(state=aloft)` (or the
pytest `state(aloft)` fixture) opens a session restored to it. The cache
key is the ROM's hash and the source of every builder in the chain, so
editing a builder, or a parent's, rebuilds it.

A state captures the machine, not the controller: a session opened from one
starts with every button released, whatever the builder held last.

The key covers the builders' own source, not helpers they call: after
changing a helper a builder relies on, delete the cache (``build/.rom_driver``
by default) to rebuild. A rebuilt ROM changes the key anyway.
"""

import hashlib
import inspect
from dataclasses import dataclass
from typing import Callable, List, Optional

_ATTR = '_rom_state'


@dataclass(frozen=True)
class StateDef:
    build: Callable
    parent: Optional[Callable]

    @property
    def name(self) -> str:
        return self.build.__name__


def rom_state(fn: Optional[Callable] = None, *, parent: Optional[Callable] = None):
    """Mark *fn* as a state's builder, starting where the state *parent* ends."""
    if parent is not None:
        definition(parent)
    def mark(f: Callable) -> Callable:
        setattr(f, _ATTR, StateDef(f, parent))
        return f
    return mark(fn) if fn is not None else mark


def definition(state: Callable) -> StateDef:
    """The `StateDef` a `rom_state` builder carries."""
    d = getattr(state, _ATTR, None)
    if d is None:
        name = getattr(state, '__name__', repr(state))
        raise TypeError(f"{name} is not a state: mark its builder with @rom_state")
    return d


def chain(state: Callable) -> List[StateDef]:
    """The state and its ancestors, root first; a cycle is an error."""
    out, seen = [], set()
    while state is not None:
        if state in seen:
            raise ValueError(f"state {state.__name__!r} is its own ancestor")
        seen.add(state)
        d = definition(state)
        out.append(d)
        state = d.parent
    return out[::-1]


def _source(f: Callable) -> str:
    try:
        return inspect.getsource(f)
    except (OSError, TypeError):
        # No source to read (built at runtime): its compiled code will do.
        code = getattr(f, '__code__', None)
        return repr((code.co_code, code.co_consts)) if code else repr(f)


def fingerprint(state: Callable) -> str:
    """What the cached state depends on, beyond the ROM: every builder's source."""
    parts = [f'{d.name}\n{_source(d.build)}' for d in chain(state)]
    return hashlib.sha1('\n'.join(parts).encode()).hexdigest()
