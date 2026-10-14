; SPDX-License-Identifier: MIT
; Copyright (c) 2026 sim09 contributors
;
; A RAM test for the real board (ROM at $FC00, no disk needed): the whole path CPU - bus - arbiter - PSRAM controller - PSRAM.
; It fills $0200-$DFFF with a pattern, reads it back and compares, for 6 patterns ($00, $FF, $A5, $5A, the low byte of the
; address, the high byte xor the low byte), and prints (to the console UART):
;     P<pattern> E<errors> A<first bad address> W<expected> R<read>
; then starts again.  Build: build.sh ramtest

ACIA	equ $E000
VMODE	equ $10			; the pattern number
VERR	equ $12			; the address after the first bad byte
VEXP	equ $14			; its expected value
VGOT	equ $15			; and what was read
VPASS	equ $16			; passes done

	org $FC00

start:	orcc #$50
	lds #$0100
	ldx #banner
	lbsr puts
	clr <VPASS
again:	clr <VMODE
pass:	ldy #0			; Y = errors of this pass
	ldx #0
	stx <VERR
	; write
	ldx #$0200
wloop:	lbsr patt
	stb ,x+
	cmpx #$E000
	lbne wloop
	; verify
	ldx #$0200
vloop:	lbsr patt
	cmpb ,x+
	beq vok
	cmpy #0
	bne vmore
	stx <VERR		; the first error: where, expected and read
	stb <VEXP
	lda -1,x
	sta <VGOT
vmore:	leay 1,y
vok:	cmpx #$E000
	lbne vloop
	; report
	lda #'P'
	lbsr putc
	lda <VMODE
	adda #'0'
	lbsr putc
	ldx #sE
	lbsr puts
	tfr y,d
	lbsr puthex16
	ldx #sA
	lbsr puts
	ldd <VERR
	lbsr puthex16
	ldx #sW
	lbsr puts
	lda <VEXP
	lbsr puthex8
	ldx #sR
	lbsr puts
	lda <VGOT
	lbsr puthex8
	ldx #crlf
	lbsr puts
	lda <VMODE
	inca
	sta <VMODE
	cmpa #6
	lblo pass
	inc <VPASS
	ldx #sDone
	lbsr puts
	lda <VPASS
	lbsr puthex8
	ldx #crlf
	lbsr puts
	lbra again

; B = the pattern byte for the address in X (A, B change)
patt:	lda <VMODE
	cmpa #4
	bhs pa
	pshs x
	ldx #pats
	ldb a,x
	puls x,pc
pa:	tfr x,d		; A = high byte of the address, B = low byte
	pshs a
	lda <VMODE
	cmpa #4
	puls a			; (the flags stay)
	beq pdone		; mode 4: the low byte
	pshs a
	eorb ,s+		; mode 5: high xor low
pdone:	rts

pats:	fcb $00,$FF,$A5,$5A

puts:	lda ,x+
	beq pr
	lbsr putc
	bra puts
pr:	rts
putc:	sta ACIA+1
	rts
puthex16: pshs b
	lbsr puthex8		; A
	puls a
	; falls to puthex8 with B -> A
puthex8: pshs a
	lsra
	lsra
	lsra
	lsra
	lbsr hexd
	puls a
	anda #$0F
hexd:	adda #'0'
	cmpa #'9'
	bls hx
	adda #7
hx:	sta ACIA+1
	rts

banner:	fcc "RAMTEST $0200-$DFFF"
crlf:	fcb 13,10,0
sE:	fcc " E"
	fcb 0
sA:	fcc " A"
	fcb 0
sW:	fcc " W"
	fcb 0
sR:	fcc " R"
	fcb 0
sDone:	fcc "DONE "
	fcb 0

	rmb $FFF0-*
	fdb 0,start,start,start,start,start,start,start
