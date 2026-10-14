; SPDX-License-Identifier: MIT
; Copyright (c) 2026 sim09 contributors
;
; The FUZIX boot ROM (fuzix/bootrom.s) with a check of what it loaded, for the real board (ROM at $FC00): after the kernel
; image has been read from the card into the RAM, it prints a 16 bit sum for every 1 KB of the RAM ($0400-$FBFF),
; 8 per line, to the console UART, and then starts the kernel.  scripts/bootcheck_expect.py computes the same sums from
; the disk image.   Build: build.sh bootcheck

ACIA	equ $E000
BUF	equ $0200
FIRSTLSN equ 2
VTMP	equ $10

	org $FC00

start:	orcc #$50
	lds #$0100
	ldx #FIRSTLSN
	clr $E041
next:	clr $E042
	stx $E043
	ldd #BUF
	std $E045
	lda #$81
	sta $E040
	tst $E040
	lbne fail
	cmpx #FIRSTLSN+2		; the first two sectors: print them (hex) as they came from the card
	lbhs nodump
	pshs x,y,u
	ldx #BUF
	ldy #256
dmp:	lda ,x+
	lbsr hex8
	leay -1,y
	lbne dmp
	ldd #$0D0A
	lbsr putc16
	puls x,y,u
nodump:	leax 1,x
	ldy BUF
	ldb BUF+2
	lbeq run
	ldu #BUF+3
copy:	lda ,u+
	sta ,y+
	decb
	lbne copy
	lbra next
run:	sty <VTMP		; the entry address of the kernel
	; V1: the running sum of the first 24 bytes of the kernel
	ldx #$0400
	ldu #0
	ldy #24
v1:
	tfr u,d
	addb ,x+
	adca #0
	tfr d,u
	pshs x,y,u
	tfr u,d
	lbsr hex16
	lda #' '
	lbsr putc
	puls x,y,u
	leay -1,y
	lbne v1
	ldd #$0D0A
	lbsr putc16
	; V2: the running sum of the first 24 bytes of the kernel
	ldx #$0400
	ldu #0
	ldy #24
v2:
	tfr u,d
	addb ,x
	leax 1,x
	adca #0
	tfr d,u
	pshs x,y,u
	tfr u,d
	lbsr hex16
	lda #' '
	lbsr putc
	puls x,y,u
	leay -1,y
	lbne v2
	ldd #$0D0A
	lbsr putc16
	; V3: the running sum of the first 24 bytes of the kernel
	ldx #$0400
	ldu #0
	ldy #24
v3:
	tfr u,d
	stb <$20
	ldb ,x+
	addb <$20
	adca #0
	tfr d,u
	pshs x,y,u
	tfr u,d
	lbsr hex16
	lda #' '
	lbsr putc
	puls x,y,u
	leay -1,y
	lbne v3
	ldd #$0D0A
	lbsr putc16
	; V4: the running sum of the first 24 bytes of the kernel
	ldx #$0400
	ldu #0
	ldy #24
v4:
	tfr u,d
	addb ,x+
	pshs cc
	adca #0
	tfr d,u
	puls cc
	pshs cc
	lda ,s+
	sta <$21
	pshs x,y,u
	tfr u,d
	lbsr hex16
	lda #' '
	lbsr putc
	puls x,y,u
	leay -1,y
	lbne v4
	ldd #$0D0A
	lbsr putc16
	ldx #$0400		; the first 64 bytes of the kernel as they are in the RAM, before anything is computed
	ldy #1024
dmp2:	lda ,x+
	lbsr hex8
	leay -1,y
	lbne dmp2
	ldd #$0D0A
	lbsr putc16
	; print the sums of the 1 KB blocks $0400 ... $FBFF
	ldx #$0400
blk:	ldu #0
	ldy #1024
sum:	tfr u,d
	addb ,x+
	adca #0
	tfr d,u
	leay -1,y
	lbne sum
	tfr u,d
	lbsr hex16
	lda #' '
	lbsr putc
	tfr x,d
	cmpa #$FC
	lbhs done
	tfr x,d
	anda #$1F		; a new line after every 8 blocks
	tstb
	lbne blk
	cmpa #0
	lbne blk
	ldd #$0D0A
	lbsr putc16
	lbra blk
done:	ldd #$0D0A
	lbsr putc16
	ldy <VTMP
	jmp ,y
fail:	lda #$A5
	sta $E00F
	bra fail

putc16:	bsr putc
	tfr b,a
putc:	sta ACIA+1
	rts
hex16:	pshs b
	lbsr hex8
	puls a
hex8:	pshs a
	lsra
	lsra
	lsra
	lsra
	bsr hd
	puls a
	anda #$0F
hd:	adda #'0'
	cmpa #'9'
	bls hx
	adda #7
hx:	sta ACIA+1
	rts

	rmb $FFF0-*
	fdb 0
	fdb $0100	; SWI3
	fdb $0103	; SWI2
	fdb $010F	; FIRQ
	fdb $010C	; IRQ
	fdb $0106	; SWI
	fdb $0109	; NMI
	fdb start	; RESET
