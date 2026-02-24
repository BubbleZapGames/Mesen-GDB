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
SANITIZER=address DEBUG=1 make  # AddressSanitizer build
SANITIZER=thread DEBUG=1 make   # ThreadSanitizer build
make clean                  # Remove all object files and binaries
```

Output binaries land in `bin/linux-x64/Release/mesen-gdb` (or `Debug/`).

## Running

```bash
# Interactive debugger
bin/linux-x64/Release/mesen-gdb game.sfc

# Batch mode: run to breakpoint, assert register/memory values
bin/linux-x64/Release/mesen-gdb game.sfc --batch --headless \
    --break $8100 --check-reg A=0x42 --check-mem 0x10=0xFF --timeout 5000

# Batch mode exit codes: 0=pass, 1=fail, 2=error/timeout
# Add --json for machine-readable output

# DAP server (for VSCode integration)
bin/linux-x64/Release/mesen-gdb --dap
```

### Interactive CLI Commands

`step [N]`, `next`, `finish`, `continue`, `regs`, `disasm [addr] [count]`, `mem <addr> <len>`, `break <addr>`, `watch <addr>`, `delete <id>`, `info break`, `info regions`, `info cpu`, `backtrace`, `trace <file>`, `dump <type> <file>`, `reset`, `set <addr> <val>`, `quit`

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

No unit test framework. Testing is done via the batch runner against compiled ROMs:

```bash
# Example: run ROM, break at $8100, verify A register equals 0x42
bin/linux-x64/Release/mesen-gdb test.sfc --batch --headless \
    --break $8100 --check-reg A=0x42 --timeout 5000
```

The R65 compiler's end-to-end tests use this binary to verify generated code.
