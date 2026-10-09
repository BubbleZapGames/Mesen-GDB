# Copyright (c) 2026 Neutron Emulation, LLC. MIT licensed.
"""pytest fixtures for ROM tests.

A project's ``conftest.py`` supplies the ROM, registers its named states,
and pulls these in:

    import pytest
    from mesen_gdb.rom_driver import Rom, rom_state
    from mesen_gdb.rom_driver.pytest_plugin import snes, state   # noqa: F401

    @pytest.fixture(scope="session")
    def rom():
        return Rom(Path(__file__).parent.parent / "build" / "GAME.smc")

    @rom_state
    def level1(s):
        s.wait(60).start().wait(120)

(Installed with its ``pytest11`` entry point, the plugin is active without
the import; importing it as well is harmless.)

``snes`` is a session at power-on. ``state`` is a factory: ``state("level1")``
returns a session restored to that state (built and cached the first time).
Every session either made is closed after the test. Run in parallel with
``pytest -n auto`` (pytest-xdist): each session is its own emulator process,
and state files are written atomically.
"""

import pytest

from .session import Rom


@pytest.fixture
def snes(rom: Rom):
    session = rom.session()
    try:
        yield session
    finally:
        session.close()


@pytest.fixture
def state(rom: Rom):
    opened = []

    def open_state(name):
        session = rom.session(state=name)
        opened.append(session)
        return session

    try:
        yield open_state
    finally:
        for session in opened:
            session.close()
