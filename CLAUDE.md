# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What This Is

Mesen-GDB is a fork of [Mesen2](https://github.com/SourMesen/Mesen2), a multi-system emulator (NES, SNES, GB, GBA, PCE, SMS, WonderSwan). This fork replaces the .NET GUI with a C++-only GDB-style CLI debugger (`mesen-gdb`), a batch test runner, and a DAP (Debug Adapter Protocol) server. It is used by the R65 compiler project for automated ROM testing.

## Build Commands

```bash
make                        # Release build with Clang (default)
make -j$(nproc)             # Parallel release build
DEBUG=1 make                # Debug build (-O0 -g)
USE_GCC=true make           # Build with GCC instead of Clang
HEADLESS=true make          # No SDL, window, sound or keyboard: runs --headless only
make TARGET=windows         # Cross-compile a headless mesen-gdb.exe with MinGW-w64 (g++-mingw-w64-x86-64-posix)
make TARGET=windows SDL2_PREFIX=~/.local/opt/SDL2-2.30.12/x86_64-w64-mingw32   # ... with the game window and sound
make test TARGET=windows    # ... and run its unit tests under Wine
SANITIZER=address DEBUG=1 make  # AddressSanitizer build
SANITIZER=thread DEBUG=1 make   # ThreadSanitizer build
make clean                  # Remove all object files and binaries
```

Output binaries land in `bin/linux-x64/Release/mesen-gdb` (or `Debug/`; `bin/windows-x64/Release/mesen-gdb.exe`
for Windows), objects in `obj.<platform>/`, each with a `.d` of the headers it includes.

## Running

```bash
# Interactive debugger
bin/linux-x64/Release/mesen-gdb game.sfc

# Batch mode: run to breakpoint, assert register/memory values
bin/linux-x64/Release/mesen-gdb game.sfc --batch --headless \
    --break $8100 --check-reg A=0x42 --check-mem 0x10=0xFF --timeout 5000

# Batch mode exit codes: 0=pass, 1=fail, 2=error/timeout
# Add --json for machine-readable output

# Test script: debugger commands from a file, with expect checks
bin/linux-x64/Release/mesen-gdb game.sfc --headless --script tests/camera
# Exit codes: 0 = every expect passed, 1 = one failed, 2 = a command failed (the script stops)

# DAP server (for VSCode integration)
bin/linux-x64/Release/mesen-gdb --dap
```

### Interactive CLI Commands

`step [N]`, `next`, `finish`, `continue`, `regs`, `disasm [addr] [count]`, `mem <addr> <len>`, `break <addr>`, `watch <addr>`, `delete <id>`, `info break`, `info regions`, `info cpu`, `backtrace`, `trace <file>`, `dump <type> <file>`, `reset`, `set <addr> <val>`, `frames <N>`, `rwatch <cond>`, `screenshot <file>`, `quit`, and:

- `until <addr> <N>` — run to `<addr>`, at most N frames (bounded in emulated time).
- `write <addr> <hex>` — several bytes at once (`write 7E0010 0a0b`).
- `jump <addr>` (`j`) — set the PC (and K) to an address or symbol without running; a bare address
  of 16 bits or less keeps the current bank. `Debugger::SetProgramCounter` only works between
  instructions, and a `frames` run stops mid-instruction, so `CmdJump` steps one instruction first
  when the first attempt does not take.
- `savestate <file>` / `loadstate <file>` — Mesen `.mss` states.
- `input <port> <buttons|number>` — hold buttons on a controller (1 = first) until the next
  `input`: a comma list of `a b x y l r select start up down left right`, `none`, or a raw
  number (decimal or `0x` hex) in the `$4218` layout (bit 15 B ... bit 4 R, bits 0-3 ignored). Live
  input, polled once a frame - the alternative to a movie. SNES only; debugger sessions
  plug a standard controller into empty ports (headless has none by default).
- `movie <file.mmo>` / `movie stop` — play a movie inside the session (power cycle, then its
  embedded `SaveState.mss` if the ROM's SHA1 matches). Breakpoints are re-applied after.
  It (and `--movie` at startup) waits for the power cycle's re-break before touching the
  debugger: a debugger call made while the emulation thread is still leaving
  `Emulator::WaitForLock` deadlocks both threads (`DebuggerCli::WaitUntilStopped`).

- `expect <what> <op> <value>` — check a register or memory value: `expect camera_x == 0x5A`,
  `expect units__x_px[3]:u16 > 0`, `expect PC == snapshot_point` (PC includes K), `expect vram 1000 == 0`.
  An `[index]` is decimal and scaled by the `:u8/:u16/:u24` width; a symbol of 1-3 bytes reads at its
  size, a bigger one needs an index and width. Values are decimal, `0x`/`$` hex, negative, or a symbol
  (a constant's value, or `name+off`). Prints PASS/FAIL (with the stdin line when scripted) and goes on; `Run()`
  returns 1 if any failed, 2 if one could not be evaluated, and `main` exits with it. JSON:
  `pass`, `actual`, `expected`, `line`.
- `info symbols [text]` — symbols whose names contain *text*.

`--script <file>` (any name, no extension expected) puts the file on stdin and calls
`DebuggerCli::SetScript`: stdout carries only results (PASS/FAIL, the stopping error, the count;
everything else goes to stderr, as in JSON mode), the first failed command stops the script with exit
2, `run`/`rwatch` give up after `--timeout` ms, and `--ram-init` defaults to zeros. A command marks
itself failed through `Fail()` (or `Usage`/`ReplyError`, which do too), so any new error path must use
it or a script will run on past the error.

Addresses are hex (`$1234`, `0x1234`, `1234`, `00:8000`) or a symbol, optionally `name+off` (hex).
`GDB/cli_symbols.*` loads them - the ROM's `.sym` and its `.dbg` (or `main.dbg`), or `--sym`/`--dbg` -
with the rules of `python/mesen_gdb/rom_driver/symbols.py` (`.sym` wins an address clash); keep the two
in step. A bare word is a symbol if one has that name, else hex. `--break` and `--check-mem*` take
symbols too. `mem <symbol>` defaults to the symbol's size; other lengths are decimal. `#` starts a
comment. `--ram-init zeros|ones|random` pins SNES power-on RAM (otherwise random), so a session
repeats exactly.

### JSON protocol (`--json` without `--batch`)

Send one command per line on stdin; every command gets
exactly **one** JSON line on stdout — the first line is `{"ok":true,"stopped":"ready",...}`.
Everything else (the ROM-load log, text-only commands like `disasm`) goes to stderr, so stdout
carries replies only. Replies are `{"ok":true,...}` or `{"ok":false,"error":"..."}`:

| Command | Extra fields |
|---|---|
| `frames`, `step`, `next`, `finish`, `run`, `until`, `jump`, `regs`, `reset`, `loadstate`, `movie` | `stopped` (`step`, `break`, `until`, ...), `frame`, `pc`, `cpu` (registers); `frames`/`until` add `frames` run, `until` adds `hit` |
| `mem`, `wram`/`vram`/... | `addr`, `hex` (lower-case byte string) |
| `dump`, `screenshot`, `savestate` | `path`, `bytes` / `frame` |
| `break`, `watch` | `id` |

A run command that exceeds its wall-clock allowance pauses the emulator and answers
`"error":"timeout"`; nothing is left running under the next command. SIGTERM (or stdin EOF)
ends the session at once; SIGINT interrupts the command in progress. A headless or `--turbo`
session re-asserts maximum speed before every run command — loading a ROM, a movie or a state
resets the rewind manager, which clears it and would otherwise pace the session at 60 fps.

### Python test driver (`python/`)

`mesen_gdb.rom_driver` speaks this protocol so SNES ROM tests run as ordinary pytest tests: chainable
live input, motions (`motion("D5R5dX5xr")`), memory by `.sym`/`.dbg` symbol name, states
(`@rom_state` builders, passed as functions: `state(level1)`) cached as save states, and
`snes`/`state` fixtures. Install with `pip install -e python`; its own tests run with
`pytest python/tests`. Usage is in [python/README.md](python/README.md). A change to a JSON reply
is a change to `python/mesen_gdb/rom_driver/mesen.py` too.

## Architecture

```
Core/              Upstream Mesen2 emulation core (read-only in practice)
  Shared/          Common abstractions (Emulator, EmuSettings, NotificationManager)
  Debugger/        Generic debugger (breakpoints, callstack, disassembly, assembler)
  SNES/            SNES emulation (65816 CPU, SPC700, DSP, coprocessors)
  NES/, Gameboy/, GBA/, PCE/, SMS/, WS/   Other console emulators

GDB/               The custom code in this fork (~1900 lines, 9 files)
  DapMain.cpp      Entry point, CLI arg parsing, emulator initialization
  batch_runner.*   Non-interactive test runner with register/memory assertions
  debugger_cli.*   Interactive GDB-style REPL
  cli_symbols.*    .sym/.dbg symbols and address parsing for the CLI and its options
  cli_platform.*   POSIX/Windows differences: stdio descriptors, home folder, stdin lines
  formatter.*      Human-readable and JSON output formatting
  cli_notification.h   Notification listener bridging emulator events to CLI
  console_info.h   Console/CPU type detection and memory region mapping

Sdl/               SDL2 rendering and audio (used for non-headless mode)
Linux/             Linux-specific input handling (libevdev bundled)
MacOS/             macOS-specific code (Objective-C++)
Utilities/         Shared utilities (VirtualFile, FolderUtilities, CRC32, etc.)
```

The GDB/ directory is where nearly all development happens. Core/ is treated as upstream and rarely modified.

## Code Style

- **Indentation**: Tabs, width 3 (enforced by `.editorconfig`)
- **C++ standard**: C++17
- **Braces**: Same line for blocks/lambdas, new line for namespaces/types/functions
- **Pointer alignment**: Left (`type* name`)
- **No space** before function parentheses or in control flow: `if(x)`, `for(...)`, `foo()`
- **Naming**: PascalCase for classes/methods, camelCase with `_` prefix for private fields (`_emu`, `_listener`)

## Dependencies

- Clang (preferred, faster binaries) or GCC with C++17 support
- SDL2 development libraries (`libsdl2-dev` on Ubuntu)
- libevdev (Linux; bundled fallback included)
- X11 (Linux)

## Testing

`make test` builds and runs `bin/dap-test` (`GDB/test_*.cpp`: the DAP, the `.dbg` parser, the CLI's
symbols). Beyond that, testing is done via the batch runner against compiled ROMs:

```bash
# Example: run ROM, break at $8100, verify A register equals 0x42
bin/linux-x64/Release/mesen-gdb test.sfc --batch --headless \
    --break $8100 --check-reg A=0x42 --timeout 5000
```

The R65 compiler's end-to-end tests use this binary to verify generated code.

CI (`.github/workflows/build.yml`, on every push and pull request) builds the Linux and Windows SDL
binaries on Ubuntu 24.04, runs `make test` for both (Windows under Wine) and `pytest python/tests`,
and uploads `mesen-gdb` and `mesen-gdb.exe` as artifacts. A `macos` job builds and tests on Apple
Silicon (`macos-26`, SDL2 linked statically via `tools/build-sdl2-static.sh`) and uploads its
binary, but releases don't include it yet. Pushing a `v*` tag (`git tag v1.2.0 && git push
origin v1.2.0`) also publishes them as a GitHub Release, once both builds pass: a Linux `.tar.gz`, a
Windows `.zip` and `SHA256SUMS`; a tag with a hyphen (`v1.2.0-rc1`) makes a prerelease.
