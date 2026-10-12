#!/bin/sh
# Build SDL2 as a static library into <prefix>, for a mesen-gdb that links
# it in (make SDL2_PREFIX=<prefix>), as the macOS build must. CI's macos job
# runs this; so can anyone building on a Mac.
#
#   tools/build-sdl2-static.sh ~/.local/opt/sdl2-static
#
# Needs curl, cmake and a C compiler.
set -eu

VERSION=2.30.12
# SDL2-2.30.12.tar.gz from SDL's GitHub release. Its .sig verifies with Sam
# Lantinga's key 1528635D8053A57F77D1E08630A59377A7763BE6, which has signed
# SDL releases since at least 2.26.0 (2022).
SHA256=ac356ea55e8b9dd0b2d1fa27da40ef7e238267ccf9324704850d5d47375b48ea

prefix=${1:?usage: $0 <prefix>}
mkdir -p "$prefix"
prefix=$(cd "$prefix" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work"

curl -sSfLO "https://github.com/libsdl-org/SDL/releases/download/release-$VERSION/SDL2-$VERSION.tar.gz"
echo "$SHA256  SDL2-$VERSION.tar.gz" | shasum -a 256 -c -
tar -xzf "SDL2-$VERSION.tar.gz"

# PIC so the library can go into a position-independent executable.
set -- -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_STATIC_PIC=ON -DSDL_TEST=OFF \
	-DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix"
if [ "$(uname -s)" = Darwin ]; then
	# The oldest macOS mesen-gdb targets (-mmacosx-version-min in the makefile).
	set -- "$@" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 -DCMAKE_OSX_ARCHITECTURES="$(uname -m)"
fi
cmake -S "SDL2-$VERSION" -B build "$@"
cmake --build build --parallel
cmake --install build
