# Mesen-GDB

Mesen-GDB is a debugger and test harness for ROM development, built on the
emulation core of [Mesen2](https://github.com/SourMesen/Mesen2). One program,
`mesen-gdb`, does four jobs:

- **A developer-focused minimal GUI.** Mesen's full GUI is scaled down for
  testing the game as you build it; it also runs in a headless CLI mode.
- **A command-line debugger.** A GDB-style prompt for stepping, breakpoints,
  watchpoints, memory, save states and live controller input.
- **A DAP debugger.** A Debug Adapter Protocol server, so VS Code (or any DAP
  client) can debug a ROM at the source line, using the compiler's `.dbg`
  symbols.
- **A unit-testing framework.** A batch mode that checks registers and memory
  for scripts and CI, a JSON protocol for programs to drive, and a Python
  package (`python/`) that runs ROM tests as ordinary pytest tests.

It emulates every system Mesen2 does: NES, SNES, Game Boy, Game Boy Advance, PC
Engine, SMS/Game Gear and WonderSwan. It was made for SNES development with the
R65 compiler, and its SNES support is the most complete: live input,
power-on RAM pinning and the Python test driver are SNES-only.

## Building

```bash
make -j$(nproc)                  # release build with Clang
DEBUG=1 make                     # debug build (-O0 -g)
USE_GCC=true make                # build with GCC instead
```

The binary is `bin/linux-x64/Release/mesen-gdb` (`Debug/` for a debug build).
It needs Clang or GCC with C++17 and the SDL2 development libraries
(`libsdl2-dev`); on Linux also X11 and libevdev (a copy is bundled). See
[COMPILING.md](COMPILING.md).

## Command-line debugger

```bash
mesen-gdb game.sfc                    # a game window plus a (mesen) prompt
mesen-gdb game.sfc --headless         # no window or audio; runs at full speed
mesen-gdb game.sfc --break 00:8000    # stop at an address before the first command
```

The ROM loads paused. Commands are typed one per line, so a script can also
be piped into stdin:

```bash
printf 'break 8100\nrun\nregs\nmem 7E0010 16\nquit\n' | mesen-gdb game.sfc --headless
```

### Commands

**Execution**

| Command | Alias | Does |
|---|---|---|
| `step [N]` | `s` | Step N instructions (default 1) |
| `next` | `n` | Step over a call |
| `finish` | | Run until the current routine returns |
| `run` | `c`, `continue` | Resume until a breakpoint |
| `frames <N>` | | Run N video frames, then stop |
| `until <addr> <N>` | | Run to `<addr>`, giving up after N frames |
| `rwatch <cond>` | `rw` | Run until a register condition holds: `A=$42`, `SP>$1FF`, `D!=0` (operators `= != < > <= >=`) |
| `reset` | | Reset the console |

**Breakpoints**

| Command | Alias | Does |
|---|---|---|
| `break <addr>` | `b` | Break when execution reaches `<addr>` |
| `watch <addr>` | | Break when `<addr>` is written |
| `delete <id>` | `del` | Remove a breakpoint or watchpoint |
| `info break` | | List breakpoints and watchpoints |

**State**

| Command | Alias | Does |
|---|---|---|
| `regs` | `r` | Show the CPU registers and flags |
| `disasm [addr] [n]` | `d` | Disassemble n lines (default: 10 at the PC) |
| `bt` | `backtrace` | Show the call stack |
| `info cpu` | | Show the console and CPU types |
| `info regions` | | List the memory regions this console has |

**Memory**

| Command | Alias | Does |
|---|---|---|
| `mem <addr> [len]` | `x` | Hex dump of CPU address space (default 256 bytes) |
| `<region> <addr> [len]` | | Hex dump of a memory region, e.g. `vram 0 64` (below) |
| `set <addr> <val>` | | Write one byte |
| `write <addr> <hex>` | | Write several bytes: `write 7E0010 0a0b0c` |
| `dump <region> <file>` | | Save a whole memory region to a file |

**Capture and replay**

| Command | Alias | Does |
|---|---|---|
| `screenshot [file]` | `ss` | Save the current frame as a PNG |
| `savestate <file>` | | Save a state (`.mss`) |
| `loadstate <file>` | | Load a state |
| `movie <file.mmo>` | | Play a Mesen movie: power cycle, then its recorded input. Breakpoints stay set |
| `movie stop` | | Stop the movie |
| `input <port> <buttons>` | | Hold buttons on a controller until the next `input` (below) |
| `trace <file> [regs]` | | Log every executed instruction to a file; `regs` adds disassembly and registers |
| `trace off` | | Stop logging |

**Session**

| Command | Alias | Does |
|---|---|---|
| `help` | `h` | List the commands |
| `quit` | `q`, `exit` | Exit |

**Addresses** are hex in any of these forms: `$1234`, `0x1234`, `1234`, or
`00:8000` (bank:address). A `mem` or region length is decimal.

**Memory regions** depend on the console; `info regions` lists them. On a SNES
they are `wram`, `vram`, `cgram`, `oam`, `rom` and `sram`. Other consoles add
their own, such as `chr`, `nt` and `pal` on the NES, `hram` on the Game Boy and
`iwram`/`ewram` on the GBA.

**Live input** (SNES): `input 1 right,b` holds Right and B on the first
controller until the next `input` for that port, and `input 1 none` releases
everything. The buttons are `a b x y l r select start up down left right`. The
game sees the new state on its next poll, so input is set between runs:

```
input 1 start
frames 3
input 1 none
frames 60
```

This is the scripted alternative to a movie. A debugger session plugs a
standard controller into any empty port, which a headless session otherwise
lacks.

**Repeatable runs:** SNES work RAM powers on random. Pass
`--ram-init zeros` (or `ones`) to pin it, so the same input gives the same run
every time.

## DAP debugger

`mesen-gdb` is a [Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/)
server, so any editor that speaks DAP can debug a ROM through it.

```bash
mesen-gdb --dap               # speaks DAP on stdin/stdout, with a game window
mesen-gdb --dap --headless    # without the window
```

Configure your editor to launch `mesen-gdb --dap` as a debug adapter. The
`launch` request takes these arguments:

| Argument | Meaning |
|---|---|
| `romPath` | The ROM to run (required) |
| `dbgPath` | A cc65-format `.dbg` symbol file. Found automatically as the ROM's name with `.dbg`, or `main.dbg` beside it |
| `stopOnEntry` | Pause before the first instruction |

With symbols loaded you can:
- set breakpoints on source lines, conditional ones included;
- step by source line, or by instruction (the `instruction` stepping
  granularity);
- read call stacks in source terms;
- view a disassembly beside the source;
- inspect **Registers** and **Flags** scopes for each CPU;
- evaluate expressions, including on hover;
- read and write memory.

Each CPU in the console (on a SNES, the 65816 and the SPC700) appears as its
own thread.

## Testing ROMs

### Batch mode

Batch mode runs to a breakpoint, checks state, and exits with a status: 0 if
every check passed, 1 if one failed, 2 on an error or timeout.

```bash
mesen-gdb game.sfc --batch --headless --break 8100 \
    --check-reg A=0x42 --check-mem 7E0010=0xFF --check-mem16 7E0020=0x1234 \
    --timeout 5000
```

Add `--movie input.mmo` to drive the controller, `--screenshot shot.png` or
`--dump vram vram.bin` to capture what it reached, and `--json` for
machine-readable output.

### JSON protocol

With `--json` and no `--batch`, the debugger becomes a protocol for other
programs. Commands are the same as at the prompt, one per line on stdin, and
every command gets exactly one JSON line on stdout. The first reply is
`{"ok":true,"stopped":"ready",...}`. Everything else (the ROM-load log, text-only
output such as `disasm`) goes to stderr, so stdout carries only replies.

```
→ until 00f4d8 600
← {"ok":true,"stopped":"until","frame":81,"pc":62680,"hit":true,"frames":81,"cpu":{"registers":{"pc":"00f4d8",...}}}
→ mem 7E0010 2
← {"ok":true,"addr":8257552,"hex":"0020"}
```

| Command | Extra reply fields |
|---|---|
| `frames`, `step`, `next`, `finish`, `run`, `until`, `regs`, `reset`, `loadstate`, `movie` | `stopped` (why it stopped), `frame`, `pc` (a number), `cpu` (registers and flags, as hex strings); `frames` and `until` add `frames` run, and `until` adds `hit` |
| `mem`, `wram`, `vram`, ... | `addr` (a number), `hex` (the bytes, lower-case) |
| `dump`, `screenshot`, `savestate` | `path`, plus `bytes` or `frame` |
| `break`, `watch` | `id` |

A failure is `{"ok":false,"error":"..."}`.

Several things keep a script from hanging:
- A run that exceeds its wall-clock allowance pauses and answers
  `"error":"timeout"`.
- SIGTERM or the end of stdin ends the session.
- SIGINT interrupts the command in progress.
- A headless session always runs at full speed.

### Python: ROM tests under pytest

`python/` holds `mesen_gdb.rom_driver`, a Python package over the JSON protocol.
With it a SNES ROM test is an ordinary pytest test. It has:
- chainable live input;
- **motions**, compact input strings for exact or overlapping presses;
- memory read by `.sym`/`.dbg` symbol name;
- named game states, built once and cached as save states;
- `snes` and `state` fixtures.

```bash
pip install -e python
```

```python
from mesen_gdb.rom_driver import motion

hadouken = motion("D5R5dX5xr")          # Down 5 frames, Down+Right 5, Right+X 5, release

def test_scroll(snes):                  # a session at power-on
    snes.wait(20).right(30)
    assert snes.var.camera_x == 0x5A

def test_special(state):                # a session restored to a named state
    s = state("level1").play(hadouken)
    assert s.var.fireballs.u8[0] == 1
```

Every call has a deadline, so a test can fail but never hang, and
`pytest -n auto` spreads the tests over the cores. See
[python/README.md](python/README.md).

## Command-line options

| Option | Meaning |
|---|---|
| `--headless` | No window or audio; runs at maximum speed |
| `--turbo` | Maximum speed with the window |
| `--break <addr>` | Set a breakpoint before starting (repeatable) |
| `--movie <file.mmo>` | Play a Mesen movie from power-on |
| `--ram-init zeros\|ones\|random` | SNES power-on RAM contents |
| `--json` | JSON output: batch results, or the JSON protocol at the prompt |
| `--batch` | Batch mode (below) |
| `--dap` | DAP mode |
| `--log-bus`, `--log-vram`, `--log-hdma` | Log SNES bus writes, VRAM writes or HDMA transfers to stdout |
| `--help` | Show the usage |

Batch mode only:

| Option | Meaning |
|---|---|
| `--timeout <ms>` | Give up after this long (default 10000) |
| `--check-reg <R>=<V>` | Assert a register |
| `--check-mem <A>=<V>` | Assert a byte |
| `--check-mem16 <A>=<V>` | Assert a word |
| `--dump <region> <file>` | Save a memory region at the stop |
| `--screenshot <file>` | Save the frame at the stop |

## License

The emulator is available under the GPL V3 license. Full text here:
<http://www.gnu.org/licenses/gpl-3.0.en.html>

The Python package in `python/` is MIT licensed.

Mesen is Copyright (C) 2014-2025 Sour; Mesen-GDB is a fork of it.

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
