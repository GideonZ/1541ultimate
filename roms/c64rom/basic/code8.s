datbk1	jsr chrget
	bit valtyp
	bpl numins
	bit inpflg
	bvc setqut
	inx
	stx txtptr
	lda #0
	sta charac
	beq resetc
setqut	sta charac
	cmp #34
	beq nowget
	lda #':'
	sta charac
	lda #44
resetc	clc
nowget	sta endchr
	lda txtptr
	ldy txtptr+1
	adc #0
	bcc nowge1
	iny
nowge1	jsr strlt2
	jsr st2txt
	jsr inpcom
	jmp strdn2
numins	jsr fin
	lda intflg
	jsr qintgr
strdn2	jsr chrgot
	beq trmok
	cmp #44
	beq *+5
	jmp trmnok
trmok	lda txtptr
	ldy txtptr+1
	sta inpptr
	sty inpptr+1
	lda vartxt
	ldy vartxt+1
	sta txtptr
	sty txtptr+1
	jsr chrgot
	beq varend
	jsr chkcom
	jmp inloop
datlop	jsr datan
	iny
	tax
	bne nowlin
	ldx #errod
	iny
	lda (txtptr),y
	beq errgo5
	iny
	lda (txtptr),y
	sta datlin
	iny
	lda (txtptr),y
	iny
	sta datlin+1
nowlin	jsr addon       ;txtptr+.y
	jsr chrgot      ;span blanks
	tax             ;used later
	cpx #datatk
	bne datlop
	jmp datbk1
varend	lda inpptr
	ldy inpptr+1
	ldx inpflg
	bpl vary0
	jmp resfin
vary0	ldy #0
	lda (inpptr),y
	beq inprts
	lda channl
	bne inprts
	lda #<exignt
	ldy #>exignt
	jmp strout
inprts	rts
exignt	.byt "?EXTRA IGNORED"
	.byt $d
	.byt 0
tryagn	.byt "?REDO FROM START"
	.byt $d
	.byt 0
next	bne getfor
	ldy #0
	beq stxfor
getfor	jsr ptrget
stxfor	sta forpnt
	sty forpnt+1
	jsr fndfor
	beq havfor
	ldx #errnf
errgo5	jmp error       ;change
havfor	txs
	txa
	clc
	adc #4
	pha
	adc #5+addprc
	sta index2
	pla
	ldy #1
	jsr movfm
	tsx
	lda addprc+264,x
	sta facsgn
	lda forpnt
	ldy forpnt+1
	jsr fadd
	jsr movvf
	ldy #1
	jsr fcompn
	tsx
	sec
	sbc addprc+264,x
	beq loopdn
	lda 269+addprc+addprc,x
	sta curlin
	lda 270+addprc+addprc,x
	sta curlin+1
	lda 272+addprc+addprc,x
	sta txtptr
	lda 271+addprc+addprc,x
	sta txtptr+1
newsgo	jmp newstt
loopdn	txa
	adc #15+addprc+addprc
	tax
	txs 
	jsr chrgot
	cmp #44
	bne newsgo
	jsr chrget
	jsr getfor
frmnum	jsr frmevl
chknum	clc
	.byt $24
chkstr	sec
chkval	bit valtyp

