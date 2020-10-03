	pha
	lda txtptr
	pha
	jsr data
	jmp deffin
getfnm	lda #fntk
	jsr synchr
	ora #128
	sta subflg
	jsr ptrgt2
	sta defpnt
	sty defpnt+1
	jmp chknum
fndoer	jsr getfnm
	lda defpnt+1
	pha
	lda defpnt
	pha
	jsr parchk
	jsr chknum
	pla
	sta defpnt
	pla
	sta defpnt+1
	ldy #2
	lda (defpnt),y
	sta varpnt
	tax
	iny
	lda (defpnt),y
	beq errguf
	sta varpnt+1
	iny
defstf	lda (varpnt),y
	pha
	dey
	bpl defstf
	ldy varpnt+1
	jsr movmf
	lda txtptr+1
	pha
	lda txtptr
	pha
	lda (defpnt),y
	sta txtptr
	iny
	lda (defpnt),y
	sta txtptr+1
	lda varpnt+1
	pha
	lda varpnt
	pha
	jsr frmnum
	pla
	sta defpnt
	pla
	sta defpnt+1
	jsr chrgot
	beq *+5
	jmp snerr
	pla
	sta txtptr
	pla
	sta txtptr+1
deffin	ldy #0
	pla
	sta (defpnt),y
	pla
	iny
	sta (defpnt),y
	pla
	iny
	sta (defpnt),y
	pla
	iny
	sta (defpnt),y
	pla
	iny
	sta (defpnt),y
	rts
strd	jsr chknum
	ldy #0
	jsr foutc
	pla
	pla
timstr	lda #<lofbuf
	ldy #>lofbuf
	beq strlit
strini	ldx facmo
	ldy facmo+1
	stx dscpnt
	sty dscpnt+1
strspa	jsr getspa
	stx dsctmp+1
	sty dsctmp+2
	sta dsctmp
	rts
strlit	ldx #34
	stx charac
	stx endchr
strlt2	sta strng1
	sty strng1+1
	sta dsctmp+1
	sty dsctmp+2
	ldy #255
strget	iny
	lda (strng1),y
	beq strfi1
	cmp charac
	beq strfin
	cmp endchr
	bne strget
strfin	cmp #34
	beq strfi2
strfi1	clc
strfi2	sty dsctmp
	tya
	adc strng1
	sta strng2
	ldx strng1+1
	bcc strst2
	inx
strst2	stx strng2+1
	lda strng1+1
	beq strcp
	cmp #bufpag
	bne putnew
strcp	tya
	jsr strini
	ldx strng1
	ldy strng1+1
	jsr movstr
putnew	ldx temppt
	cpx #tempst+strsiz+strsiz+strsiz
	bne putnw1
	ldx #errst
errgo2	jmp error
putnw1	lda dsctmp
	sta 0,x
	lda dsctmp+1
	sta 1,x
	lda dsctmp+2
	sta 2,x
	ldy #0
	stx facmo
	sty facmo+1
	sty facov
	dey
	sty valtyp
	stx lastpt
	inx
	inx
	inx
	stx temppt
	rts
getspa	lsr garbfl
tryag2	pha
	eor #255
	sec
	adc fretop
	ldy fretop+1
	bcs tryag3
	dey
tryag3	cpy strend+1
	bcc garbag
	bne strfre
	cmp strend
	bcc garbag
strfre	sta fretop
	sty fretop+1
	sta frespc
	sty frespc+1
	tax
	pla
	rts
garbag	ldx #errom
	lda garbfl
	bmi errgo2
	jsr garba2
	lda #128
	sta garbfl
	pla
	bne tryag2
garba2	ldx memsiz
	lda memsiz+1
fndvar	stx fretop
	sta fretop+1
	ldy #0
	sty grbpnt+1
	sty grbpnt
	lda strend
	ldx strend+1
	sta grbtop
	stx grbtop+1
	lda #<tempst
	ldx #>tempst
	sta index1
	stx index1+1
tvar	cmp temppt
	beq svars
	jsr dvar
	beq tvar
svars	lda #6+addprc
	sta four6
	lda vartab
	ldx vartab+1

