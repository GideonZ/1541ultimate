qint	lda facexp
	beq clrfac
	sec
	sbc #addpr8+$98
	bit facsgn
	bpl qishft
	tax
	lda #$ff
	sta bits
	jsr negfch
	txa
qishft	ldx #fac
	cmp #$f9
	bpl qint1
	jsr shiftr
	sty bits
qintrt	rts
qint1	tay
	lda facsgn
	and #$80
	lsr facho
	ora facho
	sta facho
	jsr rolshf
	sty bits
	rts
int	lda facexp
	cmp #addpr8+$98
	bcs intrts
	jsr qint
	sty facov
	lda facsgn
	sty facsgn
	eor #$80
	rol a
	lda #$98+8
	sta facexp
	lda faclo
	sta integr
	jmp fadflt
clrfac	sta facho
	sta facmoh
	sta facmo
	sta faclo
	tay 
intrts	rts
fin	ldy #$00
	ldx #$09+addprc
finzlp	sty deccnt,x
	dex
	bpl finzlp
	bcc findgq
	cmp #'-'
	bne qplus
	stx sgnflg
	beq finc
qplus	cmp #'+'
	bne fin1
finc	jsr chrget
findgq	bcc findig
fin1	cmp #'.'
	beq findp
	cmp #'E'
	bne fine
	jsr chrget
	bcc fnedg1
	cmp #minutk
	beq finec1
	cmp #'-'
	beq finec1
	cmp #plustk
	beq finec
	cmp #'+'
	beq finec
	bne finec2
finec1	ror expsgn
finec	jsr chrget
fnedg1	bcc finedg
finec2	bit expsgn
	bpl fine
	lda #0
	sec
	sbc tenexp
	jmp fine1
findp	ror dptflg
	bit dptflg
	bvc finc
fine	lda tenexp
fine1	sec
	sbc deccnt
	sta tenexp
	beq finqng
	bpl finmul
findiv	jsr div10
	inc tenexp
	bne findiv
	beq finqng
finmul	jsr mul10
	dec tenexp
	bne finmul
finqng	lda sgnflg
	bmi negxqs
	rts
negxqs	jmp negop
findig	pha
	bit dptflg
	bpl findg1
	inc deccnt
findg1	jsr mul10
	pla
	sec
	sbc #'0'
	jsr finlog
	jmp finc
finlog	pha
	jsr movaf
	pla
	jsr float
	lda argsgn
	eor facsgn
	sta arisgn
	ldx facexp
	jmp faddt
finedg	lda tenexp
	cmp #$0a
	bcc mlex10
	lda #$64
	bit expsgn
	bmi mlexmi
	jmp overr
mlex10	asl a
	asl a
	clc
	adc tenexp
	asl a
	clc
	ldy #0
	adc (txtptr),y
	sec
	sbc #'0'
mlexmi	sta tenexp
	jmp finec

