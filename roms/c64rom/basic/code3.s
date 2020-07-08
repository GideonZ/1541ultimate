list	bcc golst
	beq golst
	cmp #minutk
	bne stkrts
golst	jsr linget
	jsr fndlin
	jsr chrgot
	beq lstend
	cmp #minutk
	bne flnrts
	jsr chrget
	jsr linget
	bne flnrts
lstend	pla
	pla
	lda linnum
	ora linnum+1
	bne list4
	lda #255
	sta linnum
	sta linnum+1
list4	ldy #1
	sty dores
	lda (lowtr),y
	beq grody
	jsr iscntc
	jsr crdo
	iny
	lda (lowtr),y
	tax
	iny
	lda (lowtr),y
	cmp linnum+1
	bne tstdun
	cpx linnum
	beq typlin
tstdun	bcs grody
typlin	sty lstpnt
	jsr linprt
	lda #' '
prit4	ldy lstpnt
	and #127
ploop	jsr outdo
	cmp #34
	bne ploop1
	lda dores
	eor #$ff
	sta dores
ploop1	iny
	beq grody
	lda (lowtr),y
	bne qplop
	tay
	lda (lowtr),y
	tax
	iny
	lda (lowtr),y
	stx lowtr
	sta lowtr+1
	bne list4
grody	jmp ready
qplop	jmp (iqplop)
nqplop	bpl ploop
	cmp #pi
	beq ploop
	bit dores
	bmi ploop
	sec
	sbc #127
	tax
	sty lstpnt
	ldy #255
resrch	dex
	beq prit3
rescr1	iny
	lda reslst,y
	bpl rescr1
	bmi resrch
prit3	iny
	lda reslst,y
	bmi prit4
	jsr outdo
	bne prit3
for	lda #128
	sta subflg
	jsr let
	jsr fndfor
	bne notol
	txa
	adc #forsiz-3
	tax
	txs
notol	pla
	pla
	lda #8+addprc
	jsr getstk
	jsr datan
	clc
	tya
	adc txtptr
	pha
	lda txtptr+1
	adc #0
	pha
	lda curlin+1
	pha
	lda curlin
	pha
	lda #totk
	jsr synchr
	jsr chknum
	jsr frmnum
	lda facsgn
	ora #127
	and facho
	sta facho
	lda #<ldfone
	ldy #>ldfone
	sta index1
	sty index1+1
	jmp forpsh
ldfone	lda #<fone
	ldy #>fone
	jsr movfm
	jsr chrgot
	cmp #steptk
	bne oneon
	jsr chrget
	jsr frmnum
oneon	jsr sign
	jsr pushf
	lda forpnt+1
	pha
	lda forpnt
	pha
	lda #fortk
	pha

