## Windows

1) Open the solution in Visual Studio 2022
2) Compile as `Release`/`x64`

## Linux

To build under Linux you need a version of Clang or GCC that supports C++17.

Additionally, SDL2 must be installed.

Once SDL2 is installed, run `make` to compile with Clang.
To compile with GCC instead, use `USE_GCC=true make`.
**Note:** Mesen usually runs faster when built with Clang instead of GCC.

## macOS

To build on macOS, install SDL2 (e.g. via Homebrew).

Once SDL2 is installed, run `make`.
