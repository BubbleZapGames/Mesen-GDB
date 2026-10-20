# hello.nes: a cc65 C program for source-level debugging tests (NES).
# hello.nes and hello.dbg are committed, so using the ROM needs no cc65.
# After editing hello.c, rebuild with `make`. CC65_HOME is a cc65 checkout
# whose runtime is built for the NES (`make -C libsrc nes` there makes
# lib/nes.lib with the same cc65 as bin/).
#
# The debug flags are what source-level debugging needs: -g on cc65 and
# ca65, and --dbgfile on ld65.

CC65_HOME ?= $(HOME)/src/cc65
CC65 := $(CC65_HOME)/bin/cc65
CA65 := $(CC65_HOME)/bin/ca65
LD65 := $(CC65_HOME)/bin/ld65

all: hello.nes

hello.s: hello.c
	$(CC65) -g -Oi -t nes -I $(CC65_HOME)/include $< -o $@

hello.o: hello.s
	$(CA65) -g -t nes $< -o $@

hello.nes: hello.o
	$(LD65) -C $(CC65_HOME)/cfg/nes.cfg --dbgfile hello.dbg -o $@ $< $(CC65_HOME)/lib/nes.lib

clean:
	rm -f hello.s hello.o

.PHONY: all clean
