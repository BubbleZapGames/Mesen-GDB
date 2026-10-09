# Copyright (c) 2026 Neutron Emulation, LLC. MIT licensed.
"""Symbols for a built ROM, from the files Mesen-GDB already reads.

Two sources, because neither is complete on its own:

* the WLA-DX ``.sym`` file - the assembler's own addresses, so it is the
  truth for code labels, ROM data and ``.RAMSECTION`` statics, with a size
  wherever it defines ``_sizeof_<label>``;
* a cc65-format ``.dbg`` file - every symbol it lists with its size,
  including RAM a compiler names with ``.DEFINE`` (which the assembler never
  lists), and constants (``type=equ``). A compiler may write it before
  assembly, so its code addresses can be stale: a name both files know
  takes the ``.sym`` address.

Names are exactly as the files spell them; nothing is demangled.
"""

import re
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional


@dataclass(frozen=True)
class Symbol:
    name: str
    addr: int            # 24-bit CPU-bus address (value, for a constant)
    size: Optional[int]  # bytes, when a source says
    kind: str            # "label" (code or data) or "const"
    source: str          # "sym" or "dbg"


_SYM_LABEL = re.compile(r'^([0-9a-fA-F]{2}):([0-9a-fA-F]{4})\s+(\S+)\s*$')
_SYM_DEF = re.compile(r'^([0-9a-fA-F]{8})\s+(\S+)\s*$')


def _dbg_fields(line: str) -> Dict[str, str]:
    out = {}
    for m in re.finditer(r'(\w+)=("([^"]*)"|[^,]*)', line):
        out[m.group(1)] = m.group(3) if m.group(3) is not None else m.group(2)
    return out


class Symbols:
    """Name -> `Symbol` for one build."""

    def __init__(self, symbols: Dict[str, Symbol]):
        self._by_name = symbols

    # ---- loading ----------------------------------------------------------

    @classmethod
    def load(cls, sym: Optional[Path] = None, dbg: Optional[Path] = None) -> 'Symbols':
        """Read either file or both; with both, `.sym` wins a name clash."""
        found: Dict[str, Symbol] = {}
        if dbg is not None and Path(dbg).exists():
            found.update(cls._parse_dbg(Path(dbg).read_text()))
        if sym is not None and Path(sym).exists():
            for name, s in cls._parse_sym(Path(sym).read_text()).items():
                prior = found.get(name)
                size = s.size if s.size is not None else (prior.size if prior else None)
                found[name] = Symbol(name, s.addr, size, s.kind, 'sym')
        return cls(found)

    @staticmethod
    def _parse_sym(text: str) -> Dict[str, Symbol]:
        labels: Dict[str, int] = {}
        sizes: Dict[str, int] = {}
        section = None
        for line in text.splitlines():
            line = line.strip()
            if not line or line.startswith(';'):
                continue
            if line.startswith('['):
                section = line
                continue
            if section in ('[labels]', '[ramsections]'):
                m = _SYM_LABEL.match(line)
                if m:
                    labels[m.group(3)] = (int(m.group(1), 16) << 16) | int(m.group(2), 16)
            elif section == '[definitions]':
                m = _SYM_DEF.match(line)
                if m and m.group(2).startswith('_sizeof_'):
                    sizes[m.group(2)[len('_sizeof_'):]] = int(m.group(1), 16)
        return {label: Symbol(label, addr, sizes.get(label), 'label', 'sym')
                for label, addr in labels.items()}

    @staticmethod
    def _parse_dbg(text: str) -> Dict[str, Symbol]:
        out = {}
        for line in text.splitlines():
            if not line.startswith('sym'):
                continue
            f = _dbg_fields(line)
            name = f.get('name')
            if not name or 'val' not in f:
                continue
            addr = int(f['val'], 16)
            kind = 'const' if f.get('type') == 'equ' else 'label'
            size = int(f['size']) if 'size' in f else None
            # Data not placed yet reads 0 here; .sym has its address.
            if kind == 'label' and addr == 0 and size:
                continue
            out[name] = Symbol(name, addr, size, kind, 'dbg')
        return out

    # ---- lookup -----------------------------------------------------------

    def __contains__(self, name: str) -> bool:
        return name in self._by_name

    def __getitem__(self, name: str) -> Symbol:
        try:
            return self._by_name[name]
        except KeyError:
            near = [n for n in self._by_name if name in n][:5]
            hint = f" (similar: {', '.join(near)})" if near else ""
            raise KeyError(f"no symbol {name!r}{hint}") from None

    def get(self, name: str) -> Optional[Symbol]:
        return self._by_name.get(name)

    def names(self) -> List[str]:
        return sorted(self._by_name)

    def addr(self, name: str) -> int:
        return self[name].addr
