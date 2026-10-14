; SPDX-License-Identifier: GPL-2.0-only
	;
	; The sim09 simulator (MC6809, 64K RAM, ACIA at $E000, timer at $E030,
	; raw disk with a DMA command port at $E040, boot ROM at $FF00)
	;
	; Memory map
	;	0000-00FF	Interrupt stack (borrowed as swap stack)
	;	0100-01FF	Vectors (the ROM vectors jump here) and a few helpers
	;	0400-6BFF	Kernel code
	;	6C00-DFFF	User space (29K)
	;	E000-E1FF	I/O
	;	E200-FCFF	Kernel data (bss, data, common)
	;	FD00-FEFF	udata
	;	FF00-FFFF	boot ROM and the hardware vectors
	;

	.module sim09

	; exported
	.globl map_kernel
	.globl map_proc
	.globl map_proc_a
	.globl map_proc_always
	.globl map_save
	.globl map_restore
        .globl init_early
        .globl init_hardware
        .globl _program_vectors
	.globl _need_resched

	.globl _ramsize
	.globl _procmem

	; imported
	.globl unix_syscall_entry
	.globl null_handler
	.globl interrupt_handler

	; exported debugging tools
	.globl _plt_monitor
	.globl _plt_reboot
	.globl _plt_idle
	.globl outchar
	.globl ___hard_di
	.globl ___hard_ei
	.globl ___hard_irqrestore

	include "kernel.def"
	include "../../cpu-6809/kernel09.def"


		.area .vectors
	;
	;	The vectors of the boot ROM ($FFF2-$FFFE) jump to these
	;
	jmp badswi_handler			; 0x100 SWI3
	jmp badswi_handler			; 0x103 SWI2
	jmp unix_syscall_entry 			; 0x106 SWI (system call)
	jmp badswi_handler			; 0x109 NMI
	jmp interrupt_handler			; 0x10C IRQ (timer)
	jmp firq_handler			; 0x10F FIRQ

	.area .discard

init_early:
	ldx #null_handler
	stx 1
	lda #0x7E
	sta 0
	rts

init_hardware:
	ldd #64
	std _ramsize
	ldd #29
	std _procmem

	; Start the timer: the first IRQ comes one tick later; a write of $8F
	; to the timer register starts it and acknowledges a pending tick
	lda #0x8F
	sta TIMER
	rts

	.area .common

_plt_reboot:
	orcc #0x10
	jmp [0xFFFE]			; the boot ROM loads the kernel again

_plt_monitor:
	orcc #0x10
	lda #0xA5			; power off the simulated machine
	sta POWER
halted:	bra halted

; Wait for an interrupt: SYNC with IRQs enabled (the simulator skips the idle time)
_plt_idle:
	andcc #0xef
	sync
	rts

___hard_di:
	tfr cc,b		; return the old irq state
	orcc #0x10
	rts
___hard_ei:
	andcc #0xef
	rts

___hard_irqrestore:		; B holds the data
	tfr b,cc
	rts

;
; COMMON MEMORY PROCEDURES FOLLOW
;
;	Our vectors live in a fixed block and are not banked out. Likewise
;	No memory management: the user process always lives in 6C00-DFFF
;
_program_vectors:
map_kernel:
map_restore:
map_proc:
map_proc_always:
map_proc_a:
map_save:
	rts

firq_handler:
badswi_handler:
	rti

;
;	debug output on the console
;
outchar:
	sta ACIA0_DATA
	rts

_need_resched:
	.db 0
