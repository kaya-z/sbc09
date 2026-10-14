* SPDX-License-Identifier: MIT
* entry.s - MC6809 part of the MK09 kernel: start, trap entries, task switch, idle loop
*
* A task is always suspended with its registers on its own stack (the 12 byte frame that
* SWI / IRQ push, E flag set).  The trap entries save that stack pointer in cur_sp, switch
* to the kernel stack, call the C handler, ask the scheduler for the next task and return to
* it with RTI.  The kernel therefore runs to completion with interrupts disabled and has no
* stack of its own per task.

	include memmap.inc

	section start
	import _kmain
	import l_bss
	import s_bss
	import INITGL
	export _start
_start
	orcc	#$50			interrupts off (the boot ROM left them off already)
	lds	#_kstack+256
	ldx	#s_bss			clear the kernel bss (the kernel stack is in it)
	ldd	#l_bss
@clr	clr	,x+
	subd	#1
	bne	@clr
	lds	#_kstack+256
	lbsr	INITGL
* the vector jump table: the hardware vectors in the boot ROM point to VEC_BASE+0..15
	ldx	#VEC_BASE
	ldy	#vectab
@vec	lda	#$7E			JMP extended
	sta	,x+
	ldd	,y++
	std	,x++
	cmpy	#vectab+12
	bne	@vec
	lbsr	_kmain			returns the sp of the first task in D
	tfr	d,s
	rti

vectab	fdb	_trap_ret		SWI3
	fdb	_trap_ret		SWI2
	fdb	_swi_entry		SWI: system calls
	fdb	_trap_ret		NMI
	fdb	_irq_entry		IRQ: timer
	fdb	_trap_ret		FIRQ
	endsection

	section code
	import _cur_sp
	import _k_swi
	import _k_irq
	import _k_schedule
	export _swi_entry
	export _irq_entry
	export _trap_ret
	export _arch_idle_loop
	export _arch_copy

_swi_entry
	sts	_cur_sp
	lds	#_kstack+256
	ldx	_cur_sp
	pshs	x
	lbsr	_k_swi			void k_swi(struct frame *)
	bra	_dispatch

_irq_entry
	sts	_cur_sp
	lds	#_kstack+256
	ldx	_cur_sp
	pshs	x
	lbsr	_k_irq			void k_irq(struct frame *)
_dispatch
	lbsr	_k_schedule		u16 k_schedule(void): sp of the task to run
	tfr	d,s
_trap_ret
	rti

* the idle task: wait for the next interrupt
_arch_idle_loop
	sync
	bra	_arch_idle_loop

* void arch_copy(void *dst, const void *src, u16 n)
_arch_copy
	pshs	u
	ldu	4,s			dst
	ldx	6,s			src
	ldd	8,s			n
	beq	@done
	lsra
	rorb				D = n / 2, carry = n & 1
	bcc	@even
	pshs	a,b
	lda	,x+
	sta	,u+
	puls	a,b
@even	tfr	d,y
	leay	,y			(TFR does not set the flags)
	beq	@done
@loop	ldd	,x++
	std	,u++
	leay	-1,y
	bne	@loop
@done	puls	u,pc
	endsection

	section bss
	export _kstack
_kstack	rmb	256
	endsection

	section initgl_start
INITGL	export
INITGL	equ	*
	endsection
	section initgl_end
	rts
	endsection
