# Copyright (c) 2026 Neutron Emulation, LLC. MIT licensed.
"""Named game states: a test that starts at "level 1" rather than power-on.

A state is a builder - a function that drives a `Session` to the point the
state names - registered by name:

    from mesen_gdb.rom_driver import rom_state

    @rom_state
    def boarded(s):
        s.press("l", frames=30).press("l", "right", frames=3).wait(3) ...

    @rom_state(parent="boarded")          # starts where `boarded` ends
    def aloft(s):
        s.up(20).right(40)

`Rom.state("aloft")` builds the chain once, saves the machine as a save
state and caches it beside the ROM; `Rom.session(state="aloft")` (or the
pytest `state("aloft")` fixture) opens a session restored to it. The cache
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
from typing import Callable, Dict, Optional

_REGISTRY: Dict[str, 'StateDef'] = {}


@dataclass(frozen=True)
class StateDef:
    name: str
    build: Callable
    parent: Optional[str]


def rom_state(fn: Optional[Callable] = None, *, name: Optional[str] = None,
              parent: Optional[str] = None):
    """Register *fn* as the builder of the state *name* (default: its name)."""
    def register(f: Callable) -> Callable:
        key = name or f.__name__
        _REGISTRY[key] = StateDef(key, f, parent)
        return f
    return register(fn) if fn is not None else register


def lookup(name: str) -> StateDef:
    try:
        return _REGISTRY[name]
    except KeyError:
        known = ', '.join(sorted(_REGISTRY)) or 'none registered'
        raise KeyError(f"no state {name!r} (known: {known})") from None


def chain(name: str):
    """The state and its ancestors, root first; a cycle is an error."""
    out, seen = [], set()
    while name is not None:
        if name in seen:
            raise ValueError(f"state {name!r} is its own ancestor")
        seen.add(name)
        d = lookup(name)
        out.append(d)
        name = d.parent
    return out[::-1]


def _source(f: Callable) -> str:
    try:
        return inspect.getsource(f)
    except (OSError, TypeError):
        # No source to read (built at runtime): its compiled code will do.
        code = getattr(f, '__code__', None)
        return repr((code.co_code, code.co_consts)) if code else repr(f)


def fingerprint(name: str) -> str:
    """What the cached state depends on, beyond the ROM: every builder's source."""
    parts = [f'{d.name}\n{_source(d.build)}' for d in chain(name)]
    return hashlib.sha1('\n'.join(parts).encode()).hexdigest()
