; hello.s: an assembly-only test ROM for source-level debugging (ca65, NES).
; reset loops calling tick, so there are lines to step over, a subroutine to
; step into, and a call stack two frames deep.

.segment "HEADER"
	.byte "NES", $1A	; iNES
	.byte 2			; 2 x 16KB PRG-ROM
	.byte 1			; 1 x 8KB CHR-ROM
	.byte 0, 0		; mapper 0, horizontal mirroring
	.res 8, 0

.segment "ZEROPAGE"
counter:	.res 1
frames:		.res 1

.segment "CODE"

.proc reset
	sei
	cld
	ldx #$FF
	txs
	lda #0
	sta counter
loop:
	jsr tick
	lda counter
	cmp #100
	bne loop
	lda #0
	sta counter
	jmp loop
.endproc

.proc tick
	inc counter
	rts
.endproc

.proc nmi
	inc frames
	rti
.endproc

.proc irq
	rti
.endproc

.segment "VECTORS"
	.word nmi, reset, irq

.segment "CHARS"
	.res 8192, 0
