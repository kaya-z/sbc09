* SPDX-License-Identifier: MIT
* mksys.s - MK09 system call stubs (C callable, CMOC calling convention: arguments on the
* stack, 16 bit each, the first one nearest to the top; result in B / D).
*
* System call convention: A = number, B = u8 argument, X, Y, U = 16 bit arguments;
* the result comes back in B (status) and X (value).
*
* Every stub is assembled alone (the Makefile assembles this file once per stub with
* -DSTUB_name), so that a program links only the stubs that it calls.

	include sysnum.inc

	section code

* name = number, arguments: dst/from in B, pointer/value in X
b_x	macro
	export	_\1
_\1	ldb	3,s
	ldx	4,s
	lda	#\2
	swi
	clra
	rts
	endm

	ifdef	STUB_mk_send
	b_x	mk_send,SYS_SEND
	endc

	ifdef	STUB_mk_recv
	b_x	mk_recv,SYS_RECV
	endc

	ifdef	STUB_mk_call
	b_x	mk_call,SYS_CALL
	endc

	ifdef	STUB_mk_reply
	b_x	mk_reply,SYS_REPLY
	endc

	ifdef	STUB_mk_notify
	b_x	mk_notify,SYS_NOTIFY
	endc

	ifdef	STUB_mk_tinfo
	b_x	mk_tinfo,SYS_TINFO
	endc

	ifdef	STUB_mk_tkill
	b_x	mk_tkill,SYS_TKILL
	endc

	ifdef	STUB_mk_callg
	export	_mk_callg
_mk_callg
	ldb	3,s
	ldx	4,s
	ldy	6,s
	lda	#SYS_CALLG
	swi
	clra
	rts
	endc

	ifdef	STUB_mk_copyfrom
	export	_mk_copyfrom
_mk_copyfrom
	pshs	u
	ldb	5,s
	ldx	6,s
	ldy	8,s
	ldu	10,s
	lda	#SYS_COPYFROM
	swi
	puls	u
	clra
	rts
	endc

	ifdef	STUB_mk_copyto
	export	_mk_copyto
_mk_copyto
	pshs	u
	ldb	5,s
	ldx	6,s
	ldy	8,s
	ldu	10,s
	lda	#SYS_COPYTO
	swi
	puls	u
	clra
	rts
	endc

	ifdef	STUB_mk_tload
	export	_mk_tload
_mk_tload
	pshs	u
	ldb	5,s
	ldx	6,s
	ldy	8,s
	ldu	10,s
	lda	#SYS_TLOAD
	swi
	puls	u
	clra
	rts
	endc

	ifdef	STUB_mk_sleep
	export	_mk_sleep
_mk_sleep
	ldx	2,s
	lda	#SYS_SLEEP
	swi
	rts
	endc

	ifdef	STUB_mk_ticks
	export	_mk_ticks
_mk_ticks
	lda	#SYS_TICKS
	swi
	tfr	x,d
	rts
	endc

	ifdef	STUB_mk_yield
	export	_mk_yield
_mk_yield
	lda	#SYS_YIELD
	swi
	rts
	endc

	ifdef	STUB_mk_exit
	export	_mk_exit
_mk_exit
	ldb	3,s
	lda	#SYS_EXIT
	swi
	bra	_mk_exit
	endc

	ifdef	STUB_mk_self
	export	_mk_self
_mk_self
	lda	#SYS_SELF
	swi
	tfr	x,d
	rts
	endc

	ifdef	STUB_mk_hook
	export	_mk_hook
_mk_hook
	ldx	2,s
	lda	#SYS_HOOK
	swi
	clra
	rts
	endc

	ifdef	STUB_mk_tstart
	export	_mk_tstart
_mk_tstart
	ldb	3,s
	lda	#SYS_TSTART
	swi
	clra
	rts
	endc

	ifdef	STUB_mk_tcreate
	export	_mk_tcreate
_mk_tcreate
	ldx	2,s
	lda	#SYS_TCREATE
	swi
	pshs	b
	ldy	5,s
	tfr	x,d
	stb	,y
	puls	b
	clra
	rts
	endc

	ifdef	STUB_mk_treap
	export	_mk_treap
_mk_treap
	lda	#SYS_TREAP
	swi
	tstb
	bne	@done
	pshs	x
	ldy	4,s			tid
	lda	,s
	sta	,y
	ldy	6,s			code
	lda	1,s
	sta	,y
	leas	2,s
	clrb
@done	clra
	rts
	endc

	ifdef	STUB_mk_power
	export	_mk_power
_mk_power
	lda	#SYS_POWER
	swi
	rts
	endc

	endsection
