* SPDX-License-Identifier: MIT
* kernel.s - the MK09 kernel in MC6809 assembler
*
* The same kernel as kernel/kernel.c + arch/m6809/entry.s (tasks, synchronous messages,
* notifications, the timer tick, grants, the scheduler), written in assembler: it is less than
* half the size of the C version and about twice as fast, and the memory of a 64KB machine
* is what the programs need.  kernel/kernel.c is the portable version for other processors
* (and the reference of this one: the tests run with both, "make KERNEL=c").
*
* Conventions:
*   - the kernel runs with interrupts masked, on its own stack, with DP = the page of its
*     variables ("<name" addressing); a task's DP is saved in its frame and comes back by RTI
*   - a system call handler gets nothing in registers; it reads its arguments from the frame
*     of the caller (frm).  It returns B = status, X = value and the carry set when the call has
*     not completed (the task is blocked or has exited: its frame is left alone)
*   - U, Y and D are scratch inside the kernel; the current task is cur / curp
*
* Task table entry (TSZ bytes):
*   state prio peer call priv gfl exitcode - nbits(2) sp(2) wake(2) ubuf(2) lo(2) hi(2)
*   gptr(2) glen(2) cpu(2) msg(10)

	include memmap.inc
	include sysnum.inc

KDP	equ	K_DATA/256

NTASK	equ	8
TSZ	equ	36
FRAME	equ	12
QUANTUM	equ	2
PRIO_IDLE equ	7
STACKGUARD equ	16
MAXCOPY	equ	1024
TID_PM	equ	4
TID_USER0 equ	5
TID_ANY	equ	$FF
TID_HW	equ	$FE
OP_NOTIFY equ	$FF
NFY_EXIT equ	2

TS_FREE	equ	0
TS_READY equ	1
TS_RECV	equ	2
TS_SEND	equ	3
TS_SLEEP equ	4
TS_ZOMBIE equ	5
TS_NEW	equ	6
TS_RUN	equ	7

E_PERM	equ	1
E_NOTASK equ	2
E_DEAD	equ	3
E_FAULT	equ	4
E_NOSLOT equ	5
E_NOMEM	equ	6
E_NOTWAIT equ	7
E_INVAL	equ	8
E_NOSYS	equ	22

GR_READ	equ	1
GR_WRITE equ	2

T_STATE	equ	0
T_PRIO	equ	1
T_PEER	equ	2
T_CALL	equ	3
T_PRIV	equ	4
T_GFL	equ	5
T_EXIT	equ	6
T_NBITS	equ	8
T_SP	equ	10
T_WAKE	equ	12
T_UBUF	equ	14
T_LO	equ	16
T_HI	equ	18
T_GPTR	equ	20
T_GLEN	equ	22
T_CPU	equ	24
T_MSG	equ	26
M_SRC	equ	0
M_B	equ	4
M_C	equ	6
MSGSZ	equ	10

F_CC	equ	0
F_A	equ	1
F_B	equ	2
F_DP	equ	3
F_X	equ	4
F_Y	equ	6
F_U	equ	8
F_PC	equ	10

TIMER	equ	$E030
POWER	equ	$E00F

* ================================================================================ variables
	section	bss
	export	kernel_dp_end
cur	rmb	1			the running task
slice	rmb	1			ticks left of its time slice
resched	rmb	1			something happened: the scheduler has to look at all tasks
hooktid	rmb	1			task that gets hookbits at every tick (0 = nobody)
mode	rmb	1			ipc: 0 send, 1 call, 2 reply; grant: GR_READ / GR_WRITE
gfl	rmb	1			ipc: grant flags of a call
dstid	rmb	1
cnt	rmb	1
pbest	rmb	1
pbp	rmb	1
hookbits rmb	2
ticks	rmb	2
curp	rmb	2			&tasks[cur]
frm	rmb	2			the frame of the task that trapped
cursp0	rmb	2
dstp	rmb	2			ipc: &tasks[dst]
ubuf	rmb	2
tmpw	rmb	2
tmpw2	rmb	2
kmsg	rmb	MSGSZ			a message being built (also the taskinfo)
tcb	rmb	11			the block of a task creation
kernel_dp_end equ *
tasks	rmb	NTASK*TSZ
idlestk	rmb	64
kstack	rmb	96
kstk_top equ	*
	endsection

* ================================================================================ start
	section	start
	import	l_bss
	import	s_bss
	export	_start
_start	orcc	#$50
	lds	#kstk_top
	ldx	#s_bss			clear the bss
	ldd	#l_bss
@clr	clr	,x+
	subd	#1
	bne	@clr
	lda	#KDP
	tfr	a,dp
* the vector jump table (the hardware vectors in the boot ROM point to VEC_BASE+0..15)
	ldx	#VEC_BASE
	ldy	#vectab
@vec	lda	#$7E			JMP extended
	sta	,x+
	ldd	,y++
	std	,x++
	cmpy	#vectab+12
	bne	@vec
* the idle task (task 0)
	ldx	#tasks
	lda	#TS_READY
	sta	T_STATE,x
	lda	#PRIO_IDLE
	sta	T_PRIO,x
	ldd	#idlestk
	std	T_LO,x
	addd	#64
	std	T_HI,x
	ldy	#idle
	ldu	#0
	lbsr	mkframe			(D = top of the stack)
* the system tasks
	ldy	#boottab
@bt	ldb	,y
	beq	@btdone
	lda	#TSZ
	mul
	addd	#tasks
	tfr	d,x
	lda	#TS_READY
	sta	T_STATE,x
	lda	1,y
	sta	T_PRIO,x
	lda	2,y
	sta	T_PRIV,x
	ldd	5,y
	std	T_LO,x
	ldd	7,y
	std	T_HI,x			(D = hi = the top of the stack)
	pshs	y
	ldy	3,y			entry
	ldu	#0
	lbsr	mkframe
	puls	y
	leay	9,y
	bra	@bt
@btdone
	lda	#$8F			the timer
	sta	TIMER
	clr	<cur
	ldx	#tasks
	stx	<curp
	clr	<slice
	lbra	sched

vectab	fdb	trap_ret		SWI3
	fdb	trap_ret		SWI2
	fdb	swi_entry		SWI: system calls
	fdb	trap_ret		NMI
	fdb	irq_entry		IRQ: the timer
	fdb	trap_ret		FIRQ

* tid, prio, priv, entry, lo, hi
boottab	fcb	1,1,1
	fdb	TTY_CODE,TTY_DATA,TTY_DATA+TTY_DSZ
	fcb	2,1,0
	fdb	DISK_CODE,DISK_DATA,DISK_DATA+DISK_DSZ
	fcb	3,1,0
	fdb	FS_CODE,FS_DATA,FS_DATA+FS_DSZ
	fcb	4,1,1
	fdb	PM_CODE,PM_DATA,PM_DATA+PM_DSZ
	fcb	0
	endsection

* ================================================================================ code
	section	code
	export	trap_ret

* mkframe: X = task, D = top of its stack, Y = entry, U = argument (X in the first run):
* make the first frame and set the task's sp
mkframe	pshs	d,x,y,u
	subd	#FRAME
	std	T_SP,x
	tfr	d,x
	lda	#$80			CC: E flag, interrupts enabled
	sta	F_CC,x
	clr	F_A,x
	clr	F_B,x
	clr	F_DP,x
	ldd	6,s			argument
	std	F_X,x
	ldd	#0
	std	F_Y,x
	std	F_U,x
	ldd	4,s			entry
	std	F_PC,x
	puls	d,x,y,u,pc

idle	sync
	bra	idle

* ------------------------------------------------------------------------ the traps
swi_entry
	sts	>cursp0			(extended: the DP of the task is still in force)
	lds	#kstk_top
	lda	#KDP
	tfr	a,dp
	ldu	<cursp0
	stu	<frm
	ldy	<curp
	stu	T_SP,y
	lbsr	chkstack
	lbcs	sched
	ldb	F_A,u			the number of the call
	cmpb	#16
	blo	@ok
	tst	T_PRIV,y
	bne	@ok
	ldb	#E_PERM
	bra	@done
@ok	cmpb	#SYS_POWER
	bhi	@nosys
	clra
	aslb
	ldx	#sctab
	ldx	d,x
	beq	@nosys
	jsr	,x			-> B = status, X = value, carry: blocked
	lbcs	sched
	ldu	<frm
	stx	F_X,u
@done	ldu	<frm
	stb	F_B,u
	bra	sched
@nosys	ldb	#E_NOSYS
	bra	@done

irq_entry
	sts	>cursp0			(extended: the DP of the task is still in force)
	lds	#kstk_top
	lda	#KDP
	tfr	a,dp
	ldu	<cursp0
	stu	<frm
	ldy	<curp
	stu	T_SP,y
	lda	TIMER
	anda	#$10
	beq	@out			(not the timer)
	lda	#$8F
	sta	TIMER
	ldd	<ticks
	addd	#1
	std	<ticks
	ldd	T_CPU,y
	addd	#1
	std	T_CPU,y
	lda	<slice
	beq	@noslice
	deca
	sta	<slice
	bne	@noslice
	lda	#1
	sta	<resched
@noslice
	ldx	#tasks+TSZ		wake the sleepers
	lda	#NTASK-1
	sta	<cnt
@sl	ldb	T_STATE,x
	cmpb	#TS_SLEEP
	bne	@nx
	ldd	<ticks
	subd	T_WAKE,x
	bmi	@nx
	pshs	x
	ldb	#0
	ldy	#0
	lbsr	wake
	puls	x
@nx	leax	TSZ,x
	dec	<cnt
	bne	@sl
	ldb	<hooktid
	beq	@out
	ldx	<hookbits
	lbsr	notify
@out	ldy	<curp
	ldu	<frm
	bsr	chkstack

* sched: run the best task: S := its frame, RTI
sched	tst	<resched
	bne	@full
	tst	<slice
	beq	@full
	ldy	<curp
	lda	T_STATE,y
	cmpa	#TS_READY
	beq	@go			nothing happened that could change the choice
@full	clr	<resched
	bsr	pick			B = the task to run
	cmpb	<cur
	bne	@new
	tst	<slice
	bne	@same
@new	lda	#QUANTUM
	sta	<slice
@same	stb	<cur
	lda	#TSZ
	mul
	addd	#tasks
	std	<curp
@go	ldy	<curp
	ldd	T_SP,y
	tfr	d,s
trap_ret
	rti

* chkstack: the frame (U) of the current task (Y) must lie in its region; if not the task is
* stopped (code $FD) and the carry is set
chkstack
	tst	<cur
	beq	@ok
	ldd	T_LO,y
	addd	#STACKGUARD
	std	<tmpw
	cmpu	<tmpw
	blo	@bad
	leax	FRAME,u
	cmpx	T_HI,y
	bhi	@bad
@ok	andcc	#$FE
	rts
@bad	ldb	<cur
	lda	#$FD
	lbsr	exit
	orcc	#1
	rts

* pick: -> B = the task to run: the best priority among the READY tasks, round robin from the
* one after the current one; the current one stays while its slice lasts and nothing more urgent waits
pick	ldb	#NTASK
	stb	<pbest
	lda	#$FF
	sta	<pbp
	lda	#NTASK
	sta	<cnt
	ldy	<curp
	ldb	<cur
@l	leay	TSZ,y
	incb
	cmpb	#NTASK
	blo	@w
	ldy	#tasks
	clrb
@w	lda	T_STATE,y
	cmpa	#TS_READY
	bne	@n
	lda	T_PRIO,y
	cmpa	<pbp
	bhs	@n
	sta	<pbp
	stb	<pbest
@n	dec	<cnt
	bne	@l
	ldy	<curp
	lda	T_STATE,y
	cmpa	#TS_READY
	bne	@best
	tst	<slice
	beq	@best
	lda	T_PRIO,y
	cmpa	<pbp
	bhi	@best
	ldb	<cur
	rts
@best	ldb	<pbest
	rts

* ------------------------------------------------------------------------ helpers
* uok: D = address, X = length, Y = a task.  Carry clear if [D, D+X) is inside the task's region.
uok	pshs	d
	pshs	x
	addd	,s++
	bcs	@bad
	cmpd	T_HI,y
	bhi	@bad
	ldd	,s
	cmpd	T_LO,y
	blo	@bad
	leas	2,s
	andcc	#$FE
	rts
@bad	leas	2,s
	orcc	#1
	rts

* copy: D bytes from X to U (forward; the blocks do not overlap).  Y is kept.
copy	cmpd	#0
	beq	@done
	pshs	y
	lsra
	rorb				D = n / 2, carry = n & 1
	bcc	@even
	pshs	a,b
	lda	,x+
	sta	,u+
	puls	a,b
@even	tfr	d,y
	leay	,y
	beq	@dy
@loop	ldd	,x++
	std	,u++
	leay	-1,y
	bne	@loop
@dy	puls	y
@done	rts

* tp: B = task id -> X = its entry in the task table (D clobbered)
tp	lda	#TSZ
	mul
	addd	#tasks
	tfr	d,x
	rts

* wake: X = task, B = status, Y = value: complete its blocked system call
wake	ldu	T_SP,x
	stb	F_B,u
	sty	F_X,u
	lda	#TS_READY
	sta	T_STATE,x
	clr	T_GFL,x
	lda	#1
	sta	<resched
	rts

* deliver: X = a task that waits in a receive, Y = a message: copy it to its buffer and wake it
deliver	pshs	x,y
	ldu	T_UBUF,x
	tfr	y,x
	ldd	#MSGSZ
	bsr	copy
	ldx	,s
	ldb	#0
	ldy	#0
	bsr	wake
	puls	x,y,pc

* notify: B = task id, X = bits -> B = status
notify	cmpb	#NTASK
	bhs	@notask
	stx	<tmpw			the bits
	bsr	tp
	lda	T_STATE,x
	beq	@notask
	cmpa	#TS_NEW
	beq	@notask
	cmpa	#TS_ZOMBIE
	beq	@dead
	cmpa	#TS_RECV
	bne	@acc
	lda	T_PEER,x
	cmpa	#TID_ANY
	bne	@acc
	lda	#TID_HW			deliver it at once
	ldb	#OP_NOTIFY
	std	<kmsg
	ldd	<tmpw
	ora	T_NBITS,x
	orb	T_NBITS+1,x
	std	<kmsg+2
	ldd	#0
	std	<kmsg+4
	std	<kmsg+6
	std	<kmsg+8
	std	T_NBITS,x
	ldy	#kmsg
	bsr	deliver
	clrb
	rts
@acc	ldd	<tmpw
	ora	T_NBITS,x
	orb	T_NBITS+1,x
	std	T_NBITS,x
	clrb
	rts
@notask	ldb	#E_NOTASK
	rts
@dead	ldb	#E_DEAD
	rts

* exit: B = task id, A = exit code: the task becomes a zombie; everybody who waits for it gets an error
exit	sta	<tmpw2
	stb	<tmpw2+1
	lbsr	tp
	lda	#TS_ZOMBIE
	sta	T_STATE,x
	lda	<tmpw2
	sta	T_EXIT,x
	ldd	#0
	std	T_NBITS,x
	ldx	#tasks
	lda	#NTASK
	sta	<cnt
@l	lda	T_STATE,x
	cmpa	#TS_SEND
	beq	@w
	cmpa	#TS_RECV
	bne	@n
@w	ldb	T_PEER,x
	cmpb	<tmpw2+1
	bne	@n
	pshs	x
	ldb	#E_DEAD
	ldy	#0
	lbsr	wake
	puls	x
@n	leax	TSZ,x
	dec	<cnt
	bne	@l
	ldb	<tmpw2+1
	cmpb	<hooktid
	bne	@nh
	clr	<hooktid
@nh	cmpb	#TID_PM
	beq	@r
	ldb	#TID_PM
	ldx	#NFY_EXIT
	lbsr	notify
@r	rts

* ------------------------------------------------------------------------ system calls
sctab	fdb	0
	fdb	sc_send			1
	fdb	sc_recv			2
	fdb	sc_call			3
	fdb	sc_callg		4
	fdb	sc_reply		5
	fdb	sc_notify		6
	fdb	sc_copyfrom		7
	fdb	sc_copyto		8
	fdb	sc_sleep		9
	fdb	sc_ticks		10
	fdb	sc_yield		11
	fdb	sc_exit			12
	fdb	sc_self			13
	fdb	sc_tinfo		14
	fdb	0			15
	fdb	sc_hook			16
	fdb	sc_tcreate		17
	fdb	sc_tstart		18
	fdb	sc_tkill		19
	fdb	sc_tload		20
	fdb	sc_treap		21
	fdb	sc_power		22

* common ends of the handlers
okret	clrb
	ldx	#0
	andcc	#$FE
	rts
errret	ldx	#0			(B = the error)
	andcc	#$FE
	rts
blocked	orcc	#1
	rts

sc_send	clr	<mode
	clr	<gfl
	bra	ipc_send
sc_call	lda	#1
	sta	<mode
	clr	<gfl
	bra	ipc_send
sc_callg lda	#1
	sta	<mode
	ldu	<frm
	lda	F_Y+1,u
	sta	<gfl
	bra	ipc_send
sc_reply lda	#2
	sta	<mode
	clr	<gfl

* ipc_send: send / call / reply (mode, gfl) to the task in the frame's B, the message at its X
ipc_send
	ldu	<frm
	ldb	F_B,u
	cmpb	#NTASK
	lbhs	@notask
	cmpb	<cur
	lbeq	@notask
	stb	<dstid
	lbsr	tp
	stx	<dstp
	lda	T_STATE,x
	lbeq	@notask
	cmpa	#TS_NEW
	lbeq	@notask
	cmpa	#TS_ZOMBIE
	lbeq	@dead
	ldy	<curp
	ldd	F_X,u
	std	<ubuf
	ldx	#MSGSZ
	lbsr	uok
	lbcs	@fault
	ldx	<ubuf			copy the message to this task's msg
	leau	T_MSG,y
	ldd	#MSGSZ
	lbsr	copy
	ldy	<curp
	lda	<cur
	sta	T_MSG+M_SRC,y
	clr	T_GFL,y
	lda	<mode
	cmpa	#1
	bne	@nog
	lda	<gfl
	beq	@nog
	ldd	T_MSG+M_B,y		the granted area must be the caller's
	ldx	T_MSG+M_C,y
	lbsr	uok
	bcs	@fault
	ldd	T_MSG+M_B,y
	std	T_GPTR,y
	ldd	T_MSG+M_C,y
	std	T_GLEN,y
	lda	<gfl
	sta	T_GFL,y
@nog	ldx	<dstp
	lda	T_STATE,x
	cmpa	#TS_RECV
	bne	@wait
	lda	T_PEER,x
	cmpa	#TID_ANY
	beq	@here
	cmpa	<cur
	bne	@wait
@here	leay	T_MSG,y			the receiver is waiting: deliver
	lbsr	deliver
	ldy	<curp
	lda	<mode
	cmpa	#1
	lbne	okret
	lda	#TS_RECV		a call: now wait for the reply
	sta	T_STATE,y
	lda	<dstid
	sta	T_PEER,y
	ldd	<ubuf
	std	T_UBUF,y
	lbra	blocked
@wait	lda	<mode
	cmpa	#2
	beq	@notwait
	lda	#TS_SEND
	sta	T_STATE,y
	lda	<dstid
	sta	T_PEER,y
	lda	<mode
	sta	T_CALL,y
	ldd	<ubuf
	std	T_UBUF,y
	lbra	blocked
@notask	ldb	#E_NOTASK
	lbra	errret
@dead	ldb	#E_DEAD
	lbra	errret
@fault	ldb	#E_FAULT
	lbra	errret
@notwait ldb	#E_NOTWAIT
	lbra	errret

* sc_recv: the sender is the frame's B (TID_ANY: anybody and notifications), the buffer its X
sc_recv	ldu	<frm
	ldy	<curp
	ldd	F_X,u
	std	<ubuf
	ldx	#MSGSZ
	lbsr	uok
	lbcs	@fault
	ldu	<frm
	ldb	F_B,u
	stb	<dstid			(the sender wanted)
	cmpb	#TID_ANY
	bne	@scan
	ldd	T_NBITS,y
	beq	@scan
	lda	#TID_HW			a pending notification
	ldb	#OP_NOTIFY
	std	<kmsg
	ldd	T_NBITS,y
	std	<kmsg+2
	ldd	#0
	std	<kmsg+4
	std	<kmsg+6
	std	<kmsg+8
	std	T_NBITS,y
	ldx	#kmsg
	ldu	<ubuf
	ldd	#MSGSZ
	lbsr	copy
	lbra	okret
@scan	ldx	#tasks			a blocked sender?
	clrb
@l	lda	T_STATE,x
	cmpa	#TS_SEND
	bne	@n
	lda	T_PEER,x
	cmpa	<cur
	bne	@n
	lda	<dstid
	cmpa	#TID_ANY
	beq	@take
	pshs	b
	cmpa	,s+
	bne	@n
@take	pshs	x
	leax	T_MSG,x
	ldu	<ubuf
	ldd	#MSGSZ
	lbsr	copy
	puls	x
	tst	T_CALL,x
	beq	@plain
	lda	#TS_RECV		a call: it waits for the reply now
	sta	T_STATE,x
	lda	<cur
	sta	T_PEER,x
	clr	T_CALL,x
	lbra	okret
@plain	ldb	#0
	ldy	#0
	lbsr	wake
	lbra	okret
@n	leax	TSZ,x
	incb
	cmpb	#NTASK
	blo	@l
	ldb	<dstid
	cmpb	#TID_ANY
	beq	@block
	cmpb	#NTASK
	bhs	@notask
	lbsr	tp
	lda	T_STATE,x
	beq	@dead
	cmpa	#TS_ZOMBIE
	beq	@dead
@block	ldy	<curp
	lda	#TS_RECV
	sta	T_STATE,y
	lda	<dstid
	sta	T_PEER,y
	ldd	<ubuf
	std	T_UBUF,y
	lbra	blocked
@fault	ldb	#E_FAULT
	lbra	errret
@notask	ldb	#E_NOTASK
	lbra	errret
@dead	ldb	#E_DEAD
	lbra	errret

sc_notify
	ldu	<frm
	ldb	F_B,u
	ldx	F_X,u
	lbsr	notify
	lbra	errret

* copyfrom / copyto: B = client, X = offset, Y = the caller's memory, U = length
sc_copyfrom
	lda	#GR_READ
	bra	grant
sc_copyto
	lda	#GR_WRITE
grant	sta	<mode
	ldu	<frm
	ldb	F_B,u
	cmpb	#NTASK
	bhs	@notask
	lbsr	tp
	stx	<dstp			the client
	lda	T_STATE,x
	cmpa	#TS_RECV
	bne	@perm
	lda	T_PEER,x
	cmpa	<cur
	bne	@perm
	lda	T_GFL,x
	anda	<mode
	beq	@perm
	ldd	F_U,u			length
	cmpd	#MAXCOPY
	bhi	@inval
	ldd	T_GLEN,x
	subd	F_X,u			glen - off
	blo	@fault			(off > glen)
	cmpd	F_U,u
	blo	@fault			(n > glen - off)
	ldy	<curp			the caller's memory must be its own
	ldd	F_Y,u
	ldx	F_U,u
	lbsr	uok
	bcs	@fault
	ldx	<dstp
	ldd	T_GPTR,x
	addd	F_X,u
	std	<tmpw			the client's address
	ldd	F_U,u
	pshs	d			(the length)
	lda	<mode
	cmpa	#GR_WRITE
	beq	@to
	ldx	<tmpw			copyfrom: client -> the caller's memory
	ldu	F_Y,u
	bra	@do
@to	ldx	F_Y,u			copyto: the caller's memory -> client
	ldu	<tmpw
@do	puls	d
	lbsr	copy
	lbra	okret
@notask	ldb	#E_NOTASK
	lbra	errret
@perm	ldb	#E_PERM
	lbra	errret
@inval	ldb	#E_INVAL
	lbra	errret
@fault	ldb	#E_FAULT
	lbra	errret

sc_sleep
	ldu	<frm
	ldx	F_X,u
	beq	@yield
	ldy	<curp
	tfr	x,d
	addd	<ticks
	std	T_WAKE,y
	lda	#TS_SLEEP
	sta	T_STATE,y
	lbra	blocked
@yield	bra	sc_yield

sc_ticks
	ldx	<ticks
	clrb
	andcc	#$FE
	rts

sc_yield
	clr	<slice
	lda	#1
	sta	<resched
	lbra	okret

sc_self	ldb	<cur
	clra
	tfr	d,x
	clrb
	andcc	#$FE
	rts

sc_exit	ldu	<frm
	lda	F_B,u
	ldb	<cur
	lbsr	exit
	lbra	blocked

* tinfo: B = task, X = the buffer for state, prio, lo, hi, sp, cpu (10 bytes)
sc_tinfo
	ldu	<frm
	ldb	F_B,u
	cmpb	#NTASK
	bhs	@notask
	stb	<dstid
	ldd	F_X,u
	std	<ubuf
	ldy	<curp
	ldx	#10
	lbsr	uok
	bcs	@fault
	ldb	<dstid
	lbsr	tp
	lda	T_STATE,x
	ldb	<dstid
	cmpb	<cur
	bne	@s
	lda	#TS_RUN
@s	sta	<kmsg
	lda	T_PRIO,x
	sta	<kmsg+1
	ldd	T_LO,x
	std	<kmsg+2
	ldd	T_HI,x
	std	<kmsg+4
	ldd	T_SP,x
	std	<kmsg+6
	ldd	T_CPU,x
	std	<kmsg+8
	ldx	#kmsg
	ldu	<ubuf
	ldd	#10
	lbsr	copy
	lbra	okret
@notask	ldb	#E_NOTASK
	lbra	errret
@fault	ldb	#E_FAULT
	lbra	errret

sc_hook	ldu	<frm
	ldx	F_X,u
	stx	<hookbits
	lda	<cur
	sta	<hooktid
	lbra	okret

* tcreate: X = the block entry, lo, hi, top, arg (16 bit each), prio (8 bit) -> X = the task id
sc_tcreate
	ldu	<frm
	ldd	F_X,u
	std	<ubuf
	ldy	<curp
	ldx	#11
	lbsr	uok
	lbcs	@fault
	ldx	<ubuf
	ldu	#tcb
	ldd	#11
	lbsr	copy
	ldd	<tcb+2			lo < hi
	cmpd	<tcb+4
	lbhs	@inval
	addd	#FRAME			top >= lo + FRAME
	cmpd	<tcb+6
	lbhi	@inval
	ldd	<tcb+6			top <= hi
	cmpd	<tcb+4
	lbhi	@inval
	ldd	<tcb			lo <= entry < hi
	cmpd	<tcb+2
	blo	@inval
	cmpd	<tcb+4
	bhs	@inval
	lda	<tcb+10			prio < PRIO_IDLE
	cmpa	#PRIO_IDLE
	bhs	@inval
	ldx	#tasks+TSZ		the region must be free
	lda	#NTASK-1
	sta	<cnt
@ov	lda	T_STATE,x
	beq	@nx
	ldd	<tcb+4			hi <= o.lo: apart
	cmpd	T_LO,x
	bls	@nx
	ldd	<tcb+2			lo >= o.hi: apart
	cmpd	T_HI,x
	bhs	@nx
	ldb	#E_NOMEM
	bra	@err
@nx	leax	TSZ,x
	dec	<cnt
	bne	@ov
	ldx	#tasks+TID_USER0*TSZ	a free slot
	ldb	#TID_USER0
@sl	lda	T_STATE,x
	beq	@found
	leax	TSZ,x
	incb
	cmpb	#NTASK
	blo	@sl
	ldb	#E_NOSLOT
	bra	@err
@found	stb	<dstid
	lda	#TS_NEW
	sta	T_STATE,x
	lda	<tcb+10
	sta	T_PRIO,x
	clr	T_PEER,x
	clr	T_CALL,x
	clr	T_PRIV,x
	clr	T_GFL,x
	ldd	#0
	std	T_NBITS,x
	std	T_CPU,x
	ldd	<tcb+2
	std	T_LO,x
	ldd	<tcb+4
	std	T_HI,x
	ldd	<tcb+6			top
	ldy	<tcb			entry
	ldu	<tcb+8			argument
	lbsr	mkframe
	ldb	<dstid
	clra
	tfr	d,x
	clrb
	andcc	#$FE
	rts
@inval	ldb	#E_INVAL
	bra	@err
@fault	ldb	#E_FAULT
@err	lbra	errret

sc_tstart
	ldu	<frm
	ldb	F_B,u
	cmpb	#TID_USER0
	blo	@notask
	cmpb	#NTASK
	bhs	@notask
	lbsr	tp
	lda	T_STATE,x
	cmpa	#TS_NEW
	bne	@notask
	lda	#TS_READY
	sta	T_STATE,x
	lda	#1
	sta	<resched
	lbra	okret
@notask	ldb	#E_NOTASK
	lbra	errret

* tkill: B = task, X = exit code
sc_tkill
	ldu	<frm
	ldb	F_B,u
	cmpb	#TID_USER0
	blo	@notask
	cmpb	#NTASK
	bhs	@notask
	lbsr	tp
	lda	T_STATE,x
	beq	@notask
	cmpa	#TS_ZOMBIE
	beq	@notask
	lda	F_X+1,u
	ldb	F_B,u
	lbsr	exit
	lbra	okret
@notask	ldb	#E_NOTASK
	lbra	errret

* tload: B = task (new), X = address in it, Y = source (the caller's), U = length
sc_tload
	ldu	<frm
	ldb	F_B,u
	cmpb	#TID_USER0
	blo	@notask
	cmpb	#NTASK
	bhs	@notask
	lbsr	tp
	lda	T_STATE,x
	cmpa	#TS_NEW
	bne	@notask
	tfr	x,y
	ldd	F_X,u
	ldx	F_U,u
	lbsr	uok
	bcs	@fault
	ldy	<curp
	ldd	F_Y,u
	ldx	F_U,u
	lbsr	uok
	bcs	@fault
	ldx	F_Y,u
	ldd	F_U,u
	pshs	d
	ldu	F_X,u
	puls	d
	lbsr	copy
	lbra	okret
@notask	ldb	#E_NOTASK
	lbra	errret
@fault	ldb	#E_FAULT
	lbra	errret

* treap: collect an exited task: X = (task id << 8) | exit code
sc_treap
	ldx	#tasks+TID_USER0*TSZ
	ldb	#TID_USER0
@l	lda	T_STATE,x
	cmpa	#TS_ZOMBIE
	beq	@found
	leax	TSZ,x
	incb
	cmpb	#NTASK
	blo	@l
	ldb	#E_NOTASK
	lbra	errret
@found	lda	T_EXIT,x
	pshs	d			(B = id, A = code)
	clr	T_STATE,x
	ldd	#0
	std	T_LO,x
	std	T_HI,x
	puls	d
	exg	a,b			D = (id << 8) | code
	tfr	d,x
	clrb
	andcc	#$FE
	rts

sc_power
	lda	#$A5
	sta	POWER
	bra	*

	endsection
