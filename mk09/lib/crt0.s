* SPDX-License-Identifier: MIT
* crt0.s - start of a system server (TTY, DISK, FS, PM): clear the bss, initialise the
* globals, run main().  The kernel starts the task here with the stack already set.
*
* A C file linked with this has int main(void).

	include sysnum.inc

	section start
	import	_mk_main
	import	l_bss
	import	s_bss
	import	INITGL
	export	_start
_start
	ldx	#s_bss
	ldd	#l_bss
	beq	@go
@clr	clr	,x+
	subd	#1
	bne	@clr
@go
	ifndef	NOFILL
* fill the unused stack with $A5 (the stk program reports how much of it a server used)
	ldx	#s_bss
	ldd	#l_bss
	leax	d,x			end of the bss
	tfr	s,d
	subd	#24
	pshs	d			(the fill stops a little below the stack)
@fill	lda	#$A5
	cmpx	,s
	bhs	@filled
	sta	,x+
	bra	@fill
@filled	leas	2,s
	endc
	lbsr	INITGL
	lbsr	_mk_main
	ldb	#0
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
