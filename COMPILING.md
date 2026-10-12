## Windows

Cross-compile from Linux with MinGW-w64: install `g++-mingw-w64-x86-64-posix`
(Ubuntu/Debian), unpack SDL's MinGW development package
(`SDL2-devel-2.x.y-mingw.tar.gz` from https://github.com/libsdl-org/SDL/releases),
and name its 64-bit prefix:

    make TARGET=windows SDL2_PREFIX=/path/to/SDL2-2.x.y/x86_64-w64-mingw32

The result, `bin/windows-x64/Release/mesen-gdb.exe`, links SDL statically and needs
only DLLs that ship with Windows. It is a console program, so stdin, stdout and the
JSON protocol work as on Linux. `make test TARGET=windows` runs the unit tests under
Wine (it needs no SDL). A different MinGW toolchain is chosen with `MINGW=<prefix>`
or `CXX=`/`CC=`.

## Linux

To build under Linux you need a version of Clang or GCC that supports C++17.

Additionally, SDL2 must be installed.

Once SDL2 is installed, run `make` to compile with Clang.
To compile with GCC instead, use `USE_GCC=true make`.
**Note:** Mesen usually runs faster when built with Clang instead of GCC.

## macOS

SDL2 is linked in statically, so the binary needs nothing but macOS (11 or later).
Build SDL from its source once (needs cmake, e.g. `brew install cmake`), then
mesen-gdb against it:

    tools/build-sdl2-static.sh ~/.local/opt/sdl2-static
    make SDL2_PREFIX=~/.local/opt/sdl2-static

The result is `bin/osx-arm64/Release/mesen-gdb` on Apple Silicon, or
`bin/osx-x64/Release/mesen-gdb` on an Intel Mac.
