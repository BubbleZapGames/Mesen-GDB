# Copyright (c) 2026 Neutron Emulation, LLC. MIT licensed.
"""An in-memory `Backend` that records every call, for the API tests."""

import json
from pathlib import Path

from mesen_gdb.rom_driver.backend import Backend, Stop


class RecordingBackend(Backend):
    """Memory is a flat 64 KB; `until` hits `hit_addr` after `hit_after` frames."""

    hit_addr = None
    hit_after = 1

    def __init__(self):
        self.calls = []
        self.mem = bytearray(0x10000)
        self.frame = 0

    def _stop(self, reason, **kw):
        return Stop(reason=reason, frame=self.frame, pc=0x8000, **kw)

    def start(self, rom):
        self.calls.append(('start',))
        return self._stop('ready')

    def set_input(self, port, buttons):
        self.calls.append(('input', port, frozenset(buttons)))

    def frames(self, n):
        self.calls.append(('frames', n))
        self.frame += n
        return self._stop('step', frames=n)

    def until(self, addr, max_frames):
        self.calls.append(('until', addr, max_frames))
        hit = addr == self.hit_addr
        ran = self.hit_after if hit else max_frames
        self.frame += ran
        return self._stop('until' if hit else 'step', frames=ran, hit=hit)

    def jump(self, addr):
        self.calls.append(('jump', addr))
        return Stop(reason='jump', frame=self.frame, pc=addr)

    def read(self, addr, n):
        return bytes(self.mem[addr:addr + n])

    def write(self, addr, data):
        self.mem[addr:addr + len(data)] = data

    def save_state(self, path):
        self.calls.append(('save', Path(path).name))
        Path(path).write_text(json.dumps({'frame': self.frame, 'mem': self.mem.hex()}))

    def load_state(self, path):
        self.calls.append(('load', Path(path).name))
        d = json.loads(Path(path).read_text())
        self.frame, self.mem = d['frame'], bytearray.fromhex(d['mem'])
        return self._stop('loadstate')

    def screenshot(self, path):
        Path(path).write_bytes(b'')

    def dump(self, region, path):
        Path(path).write_bytes(bytes(self.mem))

    def close(self):
        self.calls.append(('close',))
