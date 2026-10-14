# green.sfc: a test ROM showing a solid green screen (backdrop 0x03E0).
# screen-is-green checks it: mesen-gdb green.sfc --script screen-is-green
# blank-line-runs-nothing checks that a blank line in a script runs nothing.
#
# green.sfc, green.sym and green.dbg are committed, so using the ROM needs no
# R65 toolchain. After editing green.r65, rebuild with `make` from an R65
# checkout (this repo sits at R65/Mesen-GDB), or point R65_STDLIB at one.

R65_STDLIB ?= ../../../../stdlib
R65C ?= r65c

all: green.sfc

green.asm: green.r65
	$(R65C) $< -o $@ --cfg snes --dbg -I $(R65_STDLIB)

green.o: green.asm
	wla-65816 -o $@ $<

# r65c writes green.link, the linker's object list, beside green.asm.
green.sfc: green.o
	wlalink -S green.link $@

clean:
	rm -f green.asm green.o green.link

.PHONY: all clean
