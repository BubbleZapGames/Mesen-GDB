# hello.nes: an assembly-only ca65 program for source-level debugging tests (NES).
# hello.nes and hello.dbg are committed, so using the ROM needs no cc65.
# After editing hello.s, rebuild with `make`: only ca65 and ld65 are used.
#
# The debug flags are what source-level debugging needs: -g on ca65, and
# --dbgfile on ld65.

CC65_HOME ?= $(HOME)/src/cc65
CA65 := $(CC65_HOME)/bin/ca65
LD65 := $(CC65_HOME)/bin/ld65

all: hello.nes

hello.o: hello.s
	$(CA65) -g $< -o $@

hello.nes: hello.o hello.cfg
	$(LD65) -C hello.cfg --dbgfile hello.dbg -o $@ hello.o

clean:
	rm -f hello.o

.PHONY: all clean
