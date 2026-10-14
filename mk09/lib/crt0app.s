* SPDX-License-Identifier: MIT
* crt0app.s - start of an MK09 program (an MX executable)
*
* The file starts with this 16 byte header (read by PM):
*	+0  'M' 'K'	magic
*	+2  1		version
*	+3  flags	bit 0: the program needs the whole transient area (the shell is
*			removed from memory while it runs and loaded again afterwards)
*	+4  load address
*	+6  entry address
*	+8  reserved
*	+10 size of the bss in bytes
*	+12 stack size in bytes (STACKSZ, assemble with -DSTACKSZ=n)
*	+14 reserved
* The file is loaded at the load address as it is (header, code, initialised data); PM
* then puts the bss, the stack and the start-up block (arguments) behind it and starts the
* program at the entry with X = address of the start-up block.

	include sysnum.inc

	ifndef STACKSZ
STACKSZ	equ	512
	endc
	ifndef MXFLAGS
MXFLAGS	equ	0
	endc

	section start
	import	_crt_start
	import	l_bss
	import	s_bss
	import	s_start
	import	INITGL
	export	_start
	fcc	"MK"
	fcb	1
	fcb	MXFLAGS
	fdb	s_start
	fdb	_start
	fdb	0
	fdb	l_bss
	fdb	STACKSZ
	fdb	0
_start
	pshs	x			start-up block
	ldx	#s_bss
	ldd	#l_bss
	beq	@go
@clr	clr	,x+
	subd	#1
	bne	@clr
@go	lbsr	INITGL
	lbsr	_crt_start		never returns (exit)
	lda	#SYS_EXIT
	swi
	bra	*
	endsection

	section initgl_start
INITGL	export
INITGL	equ	*
	endsection
	section initgl_end
	rts
	endsection
