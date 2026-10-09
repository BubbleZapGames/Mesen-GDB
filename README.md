# Mesen-GDB

Mesen-GDB is a debugger and test harness for ROM development, built on the
emulation core of [Mesen2](https://github.com/SourMesen/Mesen2). One program,
`mesen-gdb`, does four jobs:

- **A developer-focused minimal GUI.** Mesen's full GUI is scaled down for
  testing the game as you build it; it also runs in a headless CLI mode.
- **A command-line debugger.** A GDB-style prompt for stepping, breakpoints,
  watchpoints, memory, save states and live controller input, with the
  build's symbol names.
- **A DAP debugger.** A Debug Adapter Protocol server, so VS Code (or any DAP
  client) can debug a ROM at the source line, using the compiler's `.dbg`
  symbols.
- **A unit-testing framework.** Test scripts of debugger commands with
  `expect` checks (`--script`), a batch mode that checks registers and memory for CI, a
  JSON protocol for programs to drive, and a Python package (`python/`) that
  runs ROM tests as ordinary pytest tests.

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
printf 'break main\nrun\nregs\nmem heli_x_px\nquit\n' | mesen-gdb game.sfc --headless
```

**Symbols.** If the build left a WLA-DX `.sym` file beside the ROM (its name
with `.sym`) or a cc65-format `.dbg` (its name with `.dbg`, or `main.dbg`
beside it), every address can be a name: `break main`, `mem heli_x_px`,
`until snapshot_point 600`. `--sym` and `--dbg` name the files instead. When
both files know a name, the `.sym` address wins; the `.dbg` adds sizes,
constants and RAM the assembler never lists.

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
| `jump <addr>` | `j` | Set the PC to an address or symbol without running. A bare address of 16 bits or less keeps the current bank. Registers and mode flags are unchanged |
| `rwatch <cond>` | `rw` | Run until a register condition holds: `A=$42`, `SP>$1FF`, `D!=0` (operators `= != < > <= >=`) |
| `reset` | | Reset the console |

**Breakpoints**

| Command | Alias | Does |
|---|---|---|
| `break <addr>` | `b` | Break when execution reaches `<addr>` |
| `watch <addr>` | | Break when `<addr>` is written |
| `delete <id>` | `del` | Remove a breakpoint or watchpoint |
| `info break` | | List breakpoints and watchpoints |
| `info symbols [text]` | `info sym` | List the symbols whose names contain `text`, with address and size |

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
| `expect <what> <op> <value>` | | Check a register or memory value (see [Test scripts](#test-scripts)) |

**Capture and replay**

| Command | Alias | Does |
|---|---|---|
| `screenshot [file]` | `ss` | Save the current frame as a PNG |
| `savestate <file>` | | Save a state (`.mss`) |
| `loadstate <file>` | | Load a state |
| `movie <file.mmo>` | | Play a Mesen movie: power cycle, then its recorded input. Breakpoints stay set |
| `movie stop` | | Stop the movie |
| `input <port> <buttons\|number>` | | Hold buttons on a controller until the next `input` (below) |
| `trace <file> [regs]` | | Log every executed instruction to a file; `regs` adds disassembly and registers |
| `trace off` | | Stop logging |

**Session**

| Command | Alias | Does |
|---|---|---|
| `help` | `h` | List the commands |
| `quit` | `q`, `exit` | Exit |

**Addresses** are hex in any of these forms: `$1234`, `0x1234`, `1234`, or
`00:8000` (bank:address), or a symbol: `heli_x_px`, or `units__hp+3` with a
hex offset. A bare word is the symbol when one has that name, and hex
otherwise; `$` or `0x` always means a number. A `mem` or region length is
decimal, and `mem <symbol>` with no length shows the symbol's size.

**Memory regions** depend on the console; `info regions` lists them. On a SNES
they are `wram`, `vram`, `cgram`, `oam`, `rom` and `sram`. Other consoles add
their own, such as `chr`, `nt` and `pal` on the NES, `hram` on the Game Boy and
`iwram`/`ewram` on the GBA.

**Live input** (SNES): `input 1 right,b` holds Right and B on the first
controller until the next `input` for that port, and `input 1 none` releases
everything. The buttons are `a b x y l r select start up down left right`.

The buttons can also be one raw number, in decimal or `0x` hex: the 16-bit word
a game reads from `$4218`, with bit 15 B, then Y, Select, Start, Up, Down, Left,
Right, A, X, L, and R at bit 4. `input 1 0x8000` holds B, `input 2 128` holds
A, and `input 1 0` releases everything. Bits 0-3 are the controller's ID bits,
not buttons, and are ignored.

The game sees the new state on its next poll, so input is set between runs:

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

### Test scripts

A test can be a plain file of debugger commands with `expect` checks. Any file
name will do:

```
# The camera follows the airframe right.
input 1 x
frames 30
input 1 none
frames 20
input 1 right     # fly right
frames 30
input 1 select
until snapshot_point 20
expect PC == snapshot_point
expect camera_x == 0x005A
expect units__hp[3]:u8 > 0
```

```bash
mesen-gdb game.sfc --headless --script tests/camera
```

```
PASS  PC == snapshot_point
PASS  camera_x == 0x005A
FAIL  tests/camera:13: units__hp[3]:u8 > 0: actual $00 (0)
expect: 2 passed, 1 failed
```

`--script` runs the file and exits with a status: 0 if every check passed, 1
if one failed, and 2 if the script could not finish. Specifically:
- **A failed check** is reported and the script goes on, so one run shows
  every mismatch.
- **A failed command** stops the script, since what follows would run from the
  wrong state: an unknown symbol, a bad argument, a check that cannot be
  evaluated, or a run that times out. It is reported as `file:line: command:
  error`.
- **Every run is bounded.** `frames`, `until` and `step` already are. `run`
  and `rwatch` give up after `--timeout` milliseconds (default 10000), so a
  breakpoint that is never reached fails the test instead of hanging it.
- **Power-on RAM is zeros** unless `--ram-init` says otherwise, so a script
  runs the same every time.
- **Only results go to stdout:** the `PASS`/`FAIL` lines, a stopped script's
  error, and the count. The commands' own output and the emulator's log go to
  stderr.

`#` starts a comment, on its own line or after a command. A script can also be
piped to the prompt (`mesen-gdb game.sfc --headless < tests/camera`); the
checks and the exit status work the same, but a failed command does not stop
it.

`expect <what> <op> <value>` checks one value:
- **`<what>`** is a register (`A`, `X`, `Y`, `SP`, `D`, `DBR`, `PS`, `K`, or
  `PC`, which includes its bank), or memory: an address or symbol with an
  optional `[index]` and width `:u8`, `:u16` or `:u24`. A symbol of 1-3 bytes
  is read at its size, and anything else as one byte unless a width is given;
  an array needs both, as in `units__x_px[3]:u16` (the index is decimal and
  scaled by the width). `<region> <addr>` reads a memory region instead:
  `expect vram 1000:u16 == 0`.
- **`<op>`** is `==`, `!=`, `<`, `>`, `<=` or `>=`, compared unsigned.
- **`<value>`** is decimal, hex with `0x` or `$`, negative (`-1` is all ones
  at the width), or a symbol: a constant's value or a label's address, with an
  optional hex offset (`camera_update+3`).

With `--json`, an `expect` answers
`{"ok":true,"pass":false,"actual":90,"expected":91,"line":11}`.

### Batch mode

Batch mode runs to a breakpoint, checks state, and exits with a status: 0 if
every check passed, 1 if one failed, 2 on an error or timeout.

```bash
mesen-gdb game.sfc --batch --headless --break 8100 \
    --check-reg A=0x42 --check-mem 7E0010=0xFF --check-mem16 7E0020=0x1234 \
    --timeout 5000
```

`--break` and the `--check-mem` addresses take symbols too:
`--break main --check-mem16 heli_x_px=0x40`.

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
| `frames`, `step`, `next`, `finish`, `run`, `until`, `jump`, `regs`, `reset`, `loadstate`, `movie` | `stopped` (why it stopped), `frame`, `pc` (a number), `cpu` (registers and flags, as hex strings); `frames` and `until` add `frames` run, and `until` adds `hit` |
| `mem`, `wram`, `vram`, ... | `addr` (a number), `hex` (the bytes, lower-case) |
| `expect` | `pass`, `actual`, `expected`, `line` |
| `info symbols` | `symbols`: a list of `name`, `addr`, `size`, `const` |
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
| `--break <addr>` | Set a breakpoint before starting (an address or symbol; repeatable) |
| `--movie <file.mmo>` | Play a Mesen movie from power-on |
| `--ram-init zeros\|ones\|random` | SNES power-on RAM contents |
| `--sym <file>` | WLA-DX symbols (default: the ROM's name with `.sym`) |
| `--dbg <file>` | cc65 `.dbg` symbols (default: the ROM's name with `.dbg`, or `main.dbg` beside it) |
| `--json` | JSON output: batch results, or the JSON protocol at the prompt |
| `--batch` | Batch mode (below) |
| `--script <file>` | Run a file of debugger commands as a test ([Test scripts](#test-scripts)) |
| `--timeout <ms>` | Batch mode: give up after this long. A script: the limit for each `run` and `rwatch`. Default 10000 |
| `--dap` | DAP mode |
| `--log-bus`, `--log-vram`, `--log-hdma` | Log SNES bus writes, VRAM writes or HDMA transfers to stdout |
| `--help` | Show the usage |

Batch mode only:

| Option | Meaning |
|---|---|
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
