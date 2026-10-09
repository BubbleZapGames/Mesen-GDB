# Copyright (c) 2026 Neutron Emulation, LLC. MIT licensed.
"""The interface a ROM test drives, whatever emulator is behind it."""

from abc import ABC, abstractmethod
from dataclasses import dataclass
from pathlib import Path
from typing import FrozenSet, Optional

# The SNES controller's buttons, as every backend names them.
BUTTONS = ('a', 'b', 'x', 'y', 'l', 'r', 'select', 'start', 'up', 'down', 'left', 'right')


class RomDriverError(Exception):
    """The emulator refused a command or could not be driven."""


class RomTimeout(RomDriverError):
    """A command did not answer within its deadline; the emulator is gone."""


class NotReached(AssertionError):
    """A run to an address ended without getting there."""


@dataclass
class Stop:
    """Where a run command left the CPU."""
    reason: str            # "ready", "step", "break", "until", "loadstate", ...
    frame: int             # the emulator's frame counter
    pc: int                # 24-bit program counter
    frames: Optional[int] = None   # frames this command ran, when it ran any
    hit: Optional[bool] = None     # for until: whether the address was reached
    registers: Optional[dict] = None


class Backend(ABC):
    """One emulator instance with one ROM loaded, paused between calls.

    `start` leaves it powered on and paused before the first frame, with
    power-on RAM fixed so runs repeat exactly, and every controller released.
    Input is live: `set_input` holds buttons from the next frame until it is
    called again.
    """

    @abstractmethod
    def start(self, rom: Path) -> Stop: ...

    @abstractmethod
    def set_input(self, port: int, buttons: FrozenSet[str]) -> None:
        """Hold *buttons* (names from `BUTTONS`) on controller *port* (1 = first)."""

    @abstractmethod
    def frames(self, n: int) -> Stop: ...

    @abstractmethod
    def until(self, addr: int, max_frames: int) -> Stop: ...

    @abstractmethod
    def jump(self, addr: int) -> Stop:
        """Set the PC to *addr* (24-bit) without running."""

    @abstractmethod
    def read(self, addr: int, n: int) -> bytes: ...

    @abstractmethod
    def write(self, addr: int, data: bytes) -> None: ...

    @abstractmethod
    def save_state(self, path: Path) -> None: ...

    @abstractmethod
    def load_state(self, path: Path) -> Stop: ...

    @abstractmethod
    def screenshot(self, path: Path) -> None: ...

    @abstractmethod
    def dump(self, region: str, path: Path) -> None: ...

    @abstractmethod
    def close(self) -> None: ...
