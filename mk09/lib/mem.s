* SPDX-License-Identifier: MIT
* mem.s - memcpy, memmove, memset for the 6809 (faster than the CMOC library versions).
* C calling convention of CMOC: arguments on the stack (16 bit, first nearest), result in D.
* Assembled once per function (-DFUNC_memcpy etc.), so that a program links what it uses.

	section code

* copy D bytes from ,X to ,U forward (clobbers A B X Y U)
fwd	macro
	cmpd	#0
	beq	@done
	lsra
	rorb				D = n / 2, carry = n & 1
	bcc	@even
	pshs	a,b
	lda	,x+
	sta	,u+
	puls	a,b
@even	tfr	d,y
	leay	,y
	beq	@done
@loop	ldd	,x++
	std	,u++
	leay	-1,y
	bne	@loop
@done
	endm

	ifdef	FUNC_memcpy
* void *memcpy(void *dst, const void *src, size_t n)
	export	_memcpy
_memcpy
	pshs	u
	ldu	4,s			dst
	ldx	6,s			src
	ldd	8,s			n
	fwd
	ldd	4,s
	puls	u,pc
	endc

	ifdef	FUNC_memmove
* void *memmove(void *dst, const void *src, size_t n)
	export	_memmove
_memmove
	pshs	u
	ldu	4,s			dst
	ldx	6,s			src
	ldd	8,s			n
	cmpu	6,s
	bls	@fwd			dst <= src: copy forward
	ldd	8,s			dst > src: copying backward is always safe
	leax	d,x			src + n
	leau	d,u			dst + n
	ldd	8,s
	beq	@end
@bwd	lda	,-x
	sta	,-u
	subd	#1
	bne	@bwd
	bra	@end
@fwd	fwd
@end	ldd	4,s
	puls	u,pc
	endc

	ifdef	FUNC_memset
* void *memset(void *s, int c, size_t n)
	export	_memset
_memset
	pshs	u
	ldu	4,s			s
	ldb	7,s			c
	ldx	8,s			n
	beq	@done
@loop	stb	,u+
	leax	-1,x
	bne	@loop
@done	ldd	4,s
	puls	u,pc
	endc

	endsection
