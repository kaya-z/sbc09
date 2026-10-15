* SPDX-License-Identifier: MIT
* Copyright (c) 2026 YUKARI Semiconductor Devices
* sfpa.s - the hot parts of the soft floating point of the BASIC (sfp.h) in 6809 assembler:
*   f32 f_add(f32 a, f32 b)    f32 f_mul(f32 a, f32 b)    f32 f_div(f32 a, f32 b)    f32 f_from_long(s32 n)
* IEEE 754 single precision, round to nearest even, no NaN / infinity / denormals (as in sfp.h, whose C
* version is the reference: tests/sfp_test.c and apps/sfptest.c compare both with the C float of the PC).
* CMOC calling convention for a function that returns a 32 bit number: the arguments are on the stack,
* the first one nearest (4 bytes each, big endian), and just below them the address where the result goes:
*
*	0,s return address   2,s address of the result   4,s first argument   8,s second argument
*
* The functions are not re-entrant (the work variables are static).  Registers D X Y are not preserved, U is not used.

	section code

	export	_f_add
	export	_f_mul
	export	_f_div
	export	_f_from_long
	export	_sfp_err

	section	bss
_sfp_err rmb	1		set to 1 on an overflow, to 2 on a division by zero (the caller clears it)
res	rmb	2		address of the result
sgn	rmb	1		sign of the result in bit 7
ex	rmb	2		biased exponent (16 bit, signed)
ea	rmb	1
eb	rmb	1
st	rmb	1		sticky bit
dd	rmb	1		exponent difference
t0	rmb	1
	rmb	4		padding: the carry chains of acc may run before P
P	rmb	6		the product, also S (the 4 byte significand, the hidden bit is bit 7 of S)
MB	rmb	4		the smaller operand of an addition
Q	rmb	4		quotient
DM	rmb	3		divisor
DR	rmb	4		remainder
am	rmb	3
bm	rmb	3

S	equ	P

	section code

* ---- common tail: normalize S (a 4 byte significand, not zero) with the exponent ex, round, pack, store at (res)
nrp	ldx	ex
nb	tst	S
	bne	nbit
	ldd	S+1			shift the bytes left by 8
	std	S
	lda	S+3
	sta	S+2
	clr	S+3
	leax	-8,x
	bra	nb
nbit	tst	S
	bmi	nrnd
nl	asl	S+3
	rol	S+2
	rol	S+1
	rol	S
	leax	-1,x
	tst	S
	bpl	nl
nrnd	lda	S+3			the guard byte
	cmpa	#$80
	blo	npk
	bhi	nup
	lda	S+2			a tie: round to even
	anda	#1
	beq	npk
nup	inc	S+2
	bne	npk
	inc	S+1
	bne	npk
	inc	S
	bne	npk
	lda	#$80			the mantissa overflowed: 1.0 * 2
	sta	S
	leax	1,x
npk	cmpx	#1
	blt	zres
	cmpx	#255
	bge	ovf
	tfr	x,d			B = exponent
	lsrb				carry = low bit of the exponent
	orb	sgn
	lda	S
	anda	#$7F
	bcc	pk1
	ora	#$80
pk1	ldx	res
	stb	,x
	sta	1,x
	ldd	S+1
	std	2,x
	rts
zres	ldx	res
	clr	,x
	clr	1,x
	clr	2,x
	clr	3,x
	rts
ovf	lda	#1
	sta	_sfp_err
	lda	sgn
	ora	#$7F
	ldx	res
	sta	,x
	lda	#$7F
	sta	1,x
	lda	#$FF
	sta	2,x
	sta	3,x
	rts

* ---- f32 f_mul(a, b)
acc	macro
	addd	\1
	std	\1
	bcc	@x
	inc	\1-1
	bne	@x
	inc	\1-2
	bne	@x
	inc	\1-3
	bne	@x
	inc	\1-4
@x
	endm

_f_mul	ldx	2,s
	stx	res
	ldb	5,s			exponent of a
	lslb
	lda	4,s
	rola
	sta	ea
	beq	zres
	ldb	9,s			exponent of b
	lslb
	lda	8,s
	rola
	sta	eb
	beq	zres
	lda	4,s
	eora	8,s
	anda	#$80
	sta	sgn
	clra				ex = ea + eb - 127
	ldb	ea
	addb	eb
	rola
	subd	#127
	std	ex
	lda	5,s			the mantissas, hidden bit set
	anda	#$7F
	ora	#$80
	sta	am
	ldd	6,s
	std	am+1
	lda	9,s
	anda	#$7F
	ora	#$80
	sta	bm
	ldd	10,s
	std	bm+1
	ldd	#0			clear the product
	std	P
	std	P+2
	std	P+4
	lda	am+2			a0 * b0   (the byte k of the product is at P+5-k)
	ldb	bm+2
	mul
	acc	P+4
	lda	am+2			a0 * b1
	ldb	bm+1
	mul
	acc	P+3
	lda	am+1			a1 * b0
	ldb	bm+2
	mul
	acc	P+3
	lda	am+2			a0 * b2
	ldb	bm
	mul
	acc	P+2
	lda	am+1			a1 * b1
	ldb	bm+1
	mul
	acc	P+2
	lda	am			a2 * b0
	ldb	bm+2
	mul
	acc	P+2
	lda	am+1			a1 * b2
	ldb	bm
	mul
	acc	P+1
	lda	am			a2 * b1
	ldb	bm+1
	mul
	acc	P+1
	lda	am			a2 * b2
	ldb	bm
	mul
	acc	P
	tst	P			2^46 <= product < 2^48
	bmi	m1
	asl	P+5			shift left once
	rol	P+4
	rol	P+3
	rol	P+2
	rol	P+1
	rol	P
	bra	m2
m1	ldx	ex
	leax	1,x
	stx	ex
m2	lda	P+4			sticky: the low two bytes
	ora	P+5
	beq	m3
	lda	P+3
	ora	#1
	sta	P+3
m3	lbra	nrp

* ---- f32 f_add(a, b)
_f_add	ldx	2,s
	stx	res
	ldb	5,s
	lslb
	lda	4,s
	rola
	sta	ea
	ldb	9,s
	lslb
	lda	8,s
	rola
	sta	eb
	tst	ea			a is zero: the result is b
	bne	a1
	ldd	8,s
	ldx	res
	std	,x
	ldd	10,s
	std	2,x
	rts
a1	tst	eb			b is zero: the result is a
	bne	a2
	ldd	4,s
	ldx	res
	std	,x
	ldd	6,s
	std	2,x
	rts
a2	lda	4,s			which has the bigger magnitude?
	anda	#$7F
	sta	t0
	lda	8,s
	anda	#$7F
	cmpa	t0
	bhi	swp
	blo	nsw
	ldd	9,s
	cmpd	5,s
	bhi	swp
	blo	nsw
	lda	11,s
	cmpa	7,s
	bhi	swp
nsw	leax	4,s			X = the bigger (L), Y = the smaller (M)
	leay	8,s
	lda	ea
	ldb	eb
	bra	a3
swp	leax	8,s
	leay	4,s
	lda	eb
	ldb	ea
a3	sta	ea			ea = exponent of L, eb = exponent of M
	stb	eb
	suba	eb
	sta	dd
	cmpa	#26
	bls	a4
	ldd	,x			the difference is too big: the result is L
	pshs	x
	ldx	res
	std	,x
	puls	x
	ldd	2,x
	ldy	res
	std	2,y
	rts
a4	lda	,x			sign of the result = sign of L
	anda	#$80
	sta	sgn
	lda	,x
	eora	,y
	anda	#$80
	sta	t0			0 = add, $80 = subtract
	lda	1,x			S = significand of L
	anda	#$7F
	ora	#$80
	sta	S
	ldd	2,x
	std	S+1
	clr	S+3
	lda	1,y			MB = significand of M
	anda	#$7F
	ora	#$80
	sta	MB
	ldd	2,y
	std	MB+1
	clr	MB+3
	clra
	ldb	ea
	std	ex
	clr	st
	ldb	dd			shift MB right by dd, remembering the bits that fall out
a5	cmpb	#8
	blo	a6
	lda	MB+3
	ora	st
	sta	st
	ldd	MB+1
	std	MB+2
	lda	MB
	sta	MB+1
	clr	MB
	ldb	dd
	subb	#8
	stb	dd
	bra	a5
a6	tstb
	beq	a8
a7	lsr	MB
	ror	MB+1
	ror	MB+2
	ror	MB+3
	bcc	a7n
	lda	#1
	sta	st
a7n	decb
	bne	a7
a8	tst	st
	beq	a9
	lda	MB+3
	ora	#1
	sta	MB+3
a9	tst	t0
	bmi	sub1
	ldd	S+2			addition
	addd	MB+2
	std	S+2
	lda	S+1
	adca	MB+1
	sta	S+1
	lda	S
	adca	MB
	sta	S
	bcc	a10
	ror	S			carry out: shift right, remember the lost bit
	ror	S+1
	ror	S+2
	ror	S+3
	bcc	a11
	lda	S+3
	ora	#1
	sta	S+3
a11	ldx	ex
	leax	1,x
	stx	ex
a10	lbra	nrp
sub1	ldd	S+2			subtraction
	subd	MB+2
	std	S+2
	lda	S+1
	sbca	MB+1
	sta	S+1
	lda	S
	sbca	MB
	sta	S
	ldd	S
	bne	a12
	ldd	S+2
	lbeq	zres
a12	lbra	nrp

* ---- f32 f_from_long(s32 n)
_f_from_long
	ldx	2,s
	stx	res
	lda	4,s
	anda	#$80
	sta	sgn
	ldd	4,s
	std	S
	ldd	6,s
	std	S+2
	tst	sgn
	beq	l1
	com	S			negate
	com	S+1
	com	S+2
	com	S+3
	inc	S+3
	bne	l1
	inc	S+2
	bne	l1
	inc	S+1
	bne	l1
	inc	S
l1	ldd	S
	bne	l2
	ldd	S+2
	lbeq	zres
l2	ldd	#158			127 + 31
	std	ex
	lbra	nrp

* ---- f32 f_div(a, b)
_f_div	ldx	2,s
	stx	res
	ldb	9,s			exponent of b
	lslb
	lda	8,s
	rola
	sta	eb
	bne	d1
	lda	#2			division by zero
	sta	_sfp_err
	ldx	res
	lda	#$7F
	sta	,x
	sta	1,x
	lda	#$FF
	sta	2,x
	sta	3,x
	rts
d1	ldb	5,s			exponent of a
	lslb
	lda	4,s
	rola
	sta	ea
	lbeq	zres
	lda	4,s
	eora	8,s
	anda	#$80
	sta	sgn
	clra				ex = ea - eb + 127
	ldb	ea
	subb	eb
	sbca	#0
	addd	#127
	std	ex
	clr	DR			R = significand of a
	lda	5,s
	anda	#$7F
	ora	#$80
	sta	DR+1
	ldd	6,s
	std	DR+2
	lda	9,s			M = significand of b
	anda	#$7F
	ora	#$80
	sta	DM
	ldd	10,s
	std	DM+1
	lbsr	dcmp			R < M ?
	bhs	d2
	asl	DR+3			R <<= 1, ex--
	rol	DR+2
	rol	DR+1
	rol	DR
	ldx	ex
	leax	-1,x
	stx	ex
d2	ldd	#0
	std	Q
	std	Q+2
	ldb	#27
	stb	t0
d3	lbsr	dcmp
	blo	d4
	ldd	DR+2			R -= M
	subd	DM+1
	std	DR+2
	lda	DR+1
	sbca	DM
	sta	DR+1
	lda	DR
	sbca	#0
	sta	DR
	orcc	#1			carry = the quotient bit
	bra	d5
d4	andcc	#$FE
d5	rol	Q+3
	rol	Q+2
	rol	Q+1
	rol	Q
	asl	DR+3
	rol	DR+2
	rol	DR+1
	rol	DR
	dec	t0
	bne	d3
	ldd	DR			sticky
	bne	d6
	ldd	DR+2
	beq	d7
d6	lda	Q+3
	ora	#1
	sta	Q+3
d7	ldb	#5			Q has 27 bits: move the hidden bit to bit 31
d8	asl	Q+3
	rol	Q+2
	rol	Q+1
	rol	Q
	decb
	bne	d8
	ldd	Q
	std	S
	ldd	Q+2
	std	S+2
	lbra	nrp

* compare R (DR, 4 bytes) with M (DM, 3 bytes): condition codes as for  R - M  (unsigned: bhs = R >= M)
dcmp	lda	DR
	bne	dge
	lda	DR+1
	cmpa	DM
	bne	dret
	ldd	DR+2
	cmpd	DM+1
dret	rts
dge	lda	#1			R >= 2^24 > M: C = 0
	cmpa	#0
	rts
