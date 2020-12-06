n32768	.byt 144,128,0,0,0
flpint	jsr ayint
	lda facmo
	ldy  faclo
	rts
intidx	jsr chrget
	jsr frmevl
posint	jsr chknum
	lda facsgn
	bmi nonono
ayint	lda facexp
	cmp #144
	bcc qintgo
	lda #<n32768
	ldy #>n32768
	jsr fcomp
nonono	bne fcerr
qintgo	jmp qint
isary	lda dimflg
	ora intflg
	pha
	lda valtyp
	pha
	ldy #0
indlop	tya
	pha
	lda varnam+1
	pha
	lda varnam
	pha
	jsr intidx
	pla
	sta varnam
	pla
	sta varnam+1
	pla
	tay
	tsx
	lda 258,x
	pha
	lda 257,x
	pha
	lda indice
	sta 258,x
	lda indice+1
	sta 257,x
	iny
	jsr chrgot
	cmp #44
	beq indlop
	sty count
	jsr chkcls
	pla
	sta valtyp
	pla
	sta intflg
	and #127
	sta dimflg
	ldx arytab
	lda arytab+1
lopfda	stx lowtr
	sta lowtr+1
	cmp strend+1
	bne lopfdv
	cpx strend
	beq notfdd
lopfdv	ldy #0
	lda (lowtr),y
	iny
	cmp varnam
	bne nmary1
	lda varnam+1
	cmp (lowtr),y
	beq gotary
nmary1	iny
	lda (lowtr),y
	clc
	adc lowtr
	tax
	iny
	lda (lowtr),y
	adc lowtr+1
	bcc lopfda
bserr	ldx #errbs
	.byt $2c
fcerr	ldx #errfc
errgo3	jmp error
gotary	ldx #errdd
	lda dimflg
	bne errgo3
	jsr fmaptr
	lda count
	ldy #4
	cmp (lowtr),y
	bne bserr
	jmp getdef
notfdd	jsr fmaptr
	jsr reason
	ldy #0
	sty curtol+1
	ldx #5
	lda varnam
	sta (lowtr),y
	bpl notflt
	dex
notflt	iny
	lda varnam+1
	sta (lowtr),y
	bpl stomlt
	dex
	dex
stomlt	stx curtol
	lda count
	iny
	iny
	iny
	sta (lowtr),y
loppta	ldx #11
	lda #0
	bit dimflg
	bvc notdim
	pla
	clc
	adc #1
	tax
	pla
	adc #0
notdim	iny
	sta (lowtr),y
	iny
	txa
	sta (lowtr),y
	jsr umult
	stx curtol
	sta curtol+1
	ldy index
	dec count
	bne loppta
	adc arypnt+1
	bcs omerr1
	sta arypnt+1
	tay
	txa
	adc arypnt
	bcc grease
	iny
	beq omerr1
grease	jsr reason
	sta strend
	sty strend+1
	lda #0
	inc curtol+1

