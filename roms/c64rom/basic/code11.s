	cmp (dsctmp+1),y
	beq nxtcmp
	ldx #$ff
	bcs docmp
	ldx #1
docmp	inx
	txa
	rol a
	and domask
	beq goflot
	lda #$ff
goflot	jmp float
dim3	jsr chkcom
dim	tax
	jsr ptrgt1
	jsr chrgot
	bne dim3
	rts
ptrget	ldx #0
	jsr chrgot
ptrgt1	stx dimflg
ptrgt2	sta varnam
	jsr chrgot
	jsr isletc
	bcs ptrgt3
interr	jmp snerr
ptrgt3	ldx #0
	stx valtyp
	stx intflg
	jsr chrget
	bcc issec
	jsr isletc
	bcc nosec
issec	tax
eatem	jsr chrget
	bcc eatem
	jsr isletc
	bcs eatem
nosec	cmp #'$'
	bne notstr
	lda #$ff
	sta valtyp
	bne turnon
notstr	cmp #'%'
	bne strnam
	lda subflg
	bne interr
	lda #128
	sta intflg
	ora varnam
	sta varnam
turnon	txa
	ora #128
	tax
	jsr chrget
strnam	stx varnam+1
	sec
	ora subflg
	sbc #40
	bne *+5
	jmp isary
	ldy #0
	sty subflg
	lda vartab
	ldx vartab+1
stxfnd	stx lowtr+1
lopfnd	sta lowtr
	cpx arytab+1
	bne lopfn
	cmp arytab
	beq notfns
lopfn	lda varnam
	cmp (lowtr),y
	bne notit
	lda varnam+1
	iny
	cmp (lowtr),y
	beq finptr
	dey
notit	clc
	lda lowtr
	adc #6+addprc
	bcc lopfnd
	inx
	bne stxfnd
isletc	cmp #'A'
	bcc islrts
	sbc #$5b
	sec
	sbc #$a5
islrts	rts
notfns	pla
	pha
zz6=isvret-1
	cmp #<zz6 
	bne notevl
ldzr	lda #<zero
	ldy #>zero
	rts
notevl	lda varnam
	ldy varnam+1
	cmp #'T'
	bne qstavr
	cpy #$c9
	beq ldzr
	cpy #$49
	bne qstavr
gobadv	jmp snerr
qstavr
	cmp #'S'
	bne varok
	cpy #'T'
	beq gobadv
varok	lda arytab
	ldy arytab+1
	sta lowtr
	sty lowtr+1
	lda strend
	ldy strend+1
	sta hightr
	sty hightr+1
	clc
	adc #6+addprc
	bcc noteve
	iny
noteve	sta highds
	sty highds+1
	jsr bltu
	lda highds
	ldy highds+1
	iny
	sta arytab
	sty arytab+1
	ldy #0
	lda varnam
	sta (lowtr),y
	iny
	lda varnam+1
	sta (lowtr),y
	lda #0
	iny
	sta (lowtr),y
	iny
	sta (lowtr),y
	iny
	sta (lowtr),y
	iny
	sta (lowtr),y
	iny
	sta (lowtr),y
finptr	lda lowtr
	clc
	adc #2
	ldy lowtr+1
	bcc finnow
	iny
finnow	sta varpnt
	sty varpnt+1
	rts
fmaptr	lda count
	asl a
	adc #5
	adc lowtr
	ldy lowtr+1
	bcc jsrgm
	iny
jsrgm	sta arypnt
	sty arypnt+1
	rts

