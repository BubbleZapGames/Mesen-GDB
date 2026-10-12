# pad.sfc: a test ROM showing controller 1: PAD holds it every frame, and the
# screen is red while a button is held. pad-follows-input checks it:
# mesen-gdb pad.sfc --script pad-follows-input
#
# pad.sfc, pad.sym and pad.dbg are committed, so using the ROM needs no
# R65 toolchain. After editing pad.r65, rebuild with `make` from an R65
# checkout (this repo sits at R65/Mesen-GDB), or point R65_STDLIB at one.

R65_STDLIB ?= ../../../../stdlib
R65C ?= r65c

all: pad.sfc

pad.asm: pad.r65
	$(R65C) $< -o $@ --cfg snes --dbg -I $(R65_STDLIB)

pad.o: pad.asm
	wla-65816 -o $@ $<

# r65c writes pad.link, the linker's object list, beside pad.asm.
pad.sfc: pad.o
	wlalink -S pad.link $@

clean:
	rm -f pad.asm pad.o pad.link

.PHONY: all clean
