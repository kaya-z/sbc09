; SPDX-License-Identifier: MIT
; sim09 boot ROM for FUZIX (256 bytes at $FF00-$FFFF, including the hardware vectors)
;
; Loads the kernel image from drive 0, starting at sector 2 (256 byte sectors; sectors 0-1
; are the partition table), and jumps to it.  The image is a sequence of 256 byte sectors
;
;	[dest hi][dest lo][n][n bytes]	copy the n bytes to dest
;	[entry hi][entry lo][0]		the last sector: jump to entry
;	[lsn hi][lsn lo][$FF]		continue with the sector lsn (the image goes on there; the n of a
;				record is at most 253, so $FF is free: the OS-9 kernel image of tools/mkkernel.py)
;
; (see mkimage.py).  The hardware vectors go to the jump table the kernel puts at $0100.

BUF	equ $0200		; sector buffer: the kernel does not use this page while it is loaded
FIRSTLSN equ 2

	org $FF00

start:	orcc #$50		; interrupts off
	lds #$0100
	ldx #FIRSTLSN
	clr $E041		; drive 0
next:	clr $E042
	stx $E043		; sector number (low 16 bits)
	ldd #BUF
	std $E045		; DMA address
	lda #$81
	sta $E040		; read
	tst $E040		; result: 0 = good
	bne fail
	leax 1,x
	ldy BUF			; destination or entry
	ldb BUF+2		; byte count
	beq run
	incb
	beq goto		; $FF: go on at the sector number in the record
	decb
	ldu #BUF+3
copy:	lda ,u+
	sta ,y+
	decb
	bne copy
	bra next
run:	jmp ,y
goto:	ldx BUF
	bra next
fail:	lda #$A5		; no boot disk: power off
	sta $E00F
	bra fail

	rmb $FFF0-*		; padding up to the vectors
	fdb 0			; $FFF0 reserved
	fdb $0100		; $FFF2 SWI3
	fdb $0103		; $FFF4 SWI2
	fdb $010F		; $FFF6 FIRQ
	fdb $010C		; $FFF8 IRQ
	fdb $0106		; $FFFA SWI (system calls)
	fdb $0109		; $FFFC NMI
	fdb start		; $FFFE RESET
