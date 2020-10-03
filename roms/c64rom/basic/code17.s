getbyt	jsr frmnum
conint	jsr posint
	ldx facmo
	bne gofuc
	ldx faclo
	jmp chrgot
val	jsr len1
	bne *+5
	jmp zerofc
	ldx txtptr
	ldy txtptr+1
	stx strng2
	sty strng2+1
	ldx index1
	stx txtptr
	clc
	adc index1
	sta index2
	ldx index1+1
	stx txtptr+1
	bcc val2
	inx
val2	stx index2+1
	ldy #0
	lda (index2),y
	pha
	tya             ;a=0
	sta (index2),y
	jsr chrgot
	jsr fin
	pla
	ldy #0
	sta (index2),y
st2txt	ldx strng2
	ldy strng2+1
	stx txtptr
	sty txtptr+1
valrts	rts
getnum	jsr frmnum
	jsr getadr
combyt	jsr chkcom
	jmp getbyt
getadr	lda facsgn
	bmi gofuc
	lda facexp
	cmp #145
	bcs gofuc
	jsr qint
	lda facmo
	ldy facmo+1
	sty poker
	sta poker+1
	rts
peek	lda poker+1
	pha
	lda poker
	pha
	jsr getadr
	ldy #0
getcon	lda (poker),y
	tay
dosgfl	pla
	sta poker
	pla
	sta poker+1
	jmp sngflt
poke	jsr getnum
	txa
	ldy #0
	sta (poker),y
	rts
fnwait	jsr getnum
	stx andmsk
	ldx #0
	jsr chrgot
	beq stordo
	jsr combyt
stordo	stx eormsk
	ldy #0
waiter	lda (poker),y
	eor eormsk
	and andmsk
	beq waiter
zerrts	rts
faddh	lda #<fhalf
	ldy #>fhalf
	jmp fadd
fsub	jsr conupk
fsubt	lda facsgn
	eor #$ff
	sta facsgn
	eor argsgn
	sta arisgn
	lda facexp
	jmp faddt
fadd5	jsr shiftr
	bcc fadd4
fadd	jsr conupk
faddt	bne *+5
	jmp movfa
	ldx facov
	stx oldov
	ldx #argexp
	lda argexp
faddc	tay
	beq zerrts
	sec
	sbc facexp
	beq fadd4
	bcc fadda
	sty facexp
	ldy argsgn
	sty facsgn
	eor #$ff
	adc #0
	ldy #0
	sty oldov
	ldx #fac
	bne fadd1
fadda	ldy #0
	sty facov
fadd1	cmp #$f9
	bmi fadd5
	tay
	lda facov
	lsr 1,x
	jsr rolshf
fadd4	bit arisgn
	bpl fadd2
	ldy #facexp
	cpx #argexp
	beq subit
	ldy #argexp
subit	sec
	eor #$ff
	adc oldov
	sta facov
	lda 3+addprc,y
	sbc 3+addprc,x
	sta faclo
	lda addprc+2,y
	sbc 2+addprc,x
	sta facmo
	lda 2,y
	sbc 2,x
	sta facmoh
	lda 1,y
	sbc 1,x
	sta facho
fadflt	bcs normal
	jsr negfac
normal	ldy #0
	tya
	clc
norm3	ldx facho
	bne norm1
	ldx facho+1
	stx facho
	ldx facmoh+1

