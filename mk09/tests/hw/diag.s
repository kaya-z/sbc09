* SPDX-License-Identifier: MIT
* diag.s - hardware check for the Tang Nano 9K: replaces the MK09 boot image on the card
* (tests/hw/README.md).  Prints a banner on the ACIA, then the first 16 bytes and the bytes
* 510/511 of some sectors that it reads with the disk registers, so that the UART and the
* card can be checked without the kernel.
BUF	equ	$0200
	org	$0300
start	lds	#$0100
	ldx	#msg
	bsr	puts
	ldx	#2		LSN 2 (the boot stream, where this program came from)
	bsr	dump
	ldx	#0		LSN 0 (the partition table)
	bsr	dump
	ldx	#1		LSN 1 (the partition table, second half: 55 AA at 254/255)
	bsr	dump
	ldx	#4096		LSN 4096 (FAT boot sector, block 2048)
	bsr	dump
	ldx	#msgend
	bsr	puts
	lda	#$A5		power off
	sta	$E00F
hang	bra	hang

* dump sector X: "LSN hhhh st=hh: 16 bytes .. last two bytes"
dump	pshs	x
	ldx	#msglsn
	bsr	puts
	ldx	,s
	tfr	x,d
	bsr	hex16
	clr	$E041
	clr	$E042
	ldx	,s
	stx	$E043
	ldd	#BUF
	std	$E045
	lda	#$81
	sta	$E040
	lda	$E040
	pshs	a
	ldx	#msgst
	bsr	puts
	puls	a
	bsr	hex8
	lda	#':'
	bsr	putc
	ldx	#BUF
	ldb	#16
d1	lda	,x+
	bsr	hex8
	lda	#' '
	bsr	putc
	decb
	bne	d1
	lda	#'.'
	bsr	putc
	lda	BUF+254
	bsr	hex8
	lda	BUF+255
	bsr	hex8
	lda	#13
	bsr	putc
	lda	#10
	bsr	putc
	puls	x,pc

puts	lda	,x+
	beq	pdone
	bsr	putc
	bra	puts
pdone	rts
putc	pshs	a
pw	lda	$E000
	anda	#2
	beq	pw
	puls	a
	sta	$E001
	rts
hex16	pshs	b
	bsr	hex8
	puls	a
hex8	pshs	a
	lsra
	lsra
	lsra
	lsra
	bsr	nib
	puls	a
	anda	#$0F
nib	adda	#'0'
	cmpa	#'9'
	bls	nibo
	adda	#7
nibo	bra	putc

msg	fcc	"MK09 DIAG"
	fcb	13,10,0
msglsn	fcc	"LSN "
	fcb	0
msgst	fcc	" st="
	fcb	0
msgend	fcc	"DIAG END"
	fcb	13,10,0
