bufofs	=$200
crunch	jmp (icrnch)
ncrnch	ldx txtptr
	ldy #4
	sty dores
kloop	lda bufofs,x
	bpl cmpspc
	cmp #pi
	beq stuffh
	inx
	bne kloop
cmpspc	cmp #' '
	beq stuffh
	sta endchr
	cmp #34
	beq strng
	bit dores
	bvs stuffh
	cmp #'?'
	bne kloop1
	lda #printk
	bne stuffh
kloop1	cmp #'0'
	bcc mustcr
	cmp #60
	bcc stuffh
mustcr	sty bufptr
	ldy #0
	sty count
	dey
	stx txtptr
	dex
reser	iny
	inx
rescon	lda bufofs,x
	sec
	sbc reslst,y
	beq reser
	cmp #128
	bne nthis
	ora count
getbpt	ldy bufptr
stuffh	inx
	iny
	sta buf-5,y
	lda buf-5,y
	beq crdone
	sec
	sbc #':'
	beq colis
	cmp #datatk-$3a
	bne nodatt
colis	sta dores
nodatt	sec
	sbc #remtk-$3a
	bne kloop
	sta endchr
str1	lda bufofs,x
	beq stuffh
	cmp endchr
	beq stuffh
strng	iny
	sta buf-5,y
	inx
	bne str1
nthis	ldx txtptr
	inc count
nthis1	iny
	lda reslst-1,y
	bpl nthis1
	lda reslst,y
	bne rescon
	lda bufofs,x
	bpl getbpt
crdone	sta buf-3,y
	dec txtptr+1
zz1	=buf-1
	lda #<zz1
	sta txtptr
	rts
fndlin	lda txttab
	ldx txttab+1
fndlnc	ldy #1
	sta lowtr
	stx lowtr+1
	lda (lowtr),y
	beq flinrt
	iny
	iny
	lda linnum+1
	cmp (lowtr),y
	bcc flnrts
	beq fndlo1 
	dey
	bne affrts
fndlo1	lda linnum
	dey
	cmp (lowtr),y
	bcc flnrts
	beq flnrts
affrts	dey
	lda (lowtr),y
	tax
	dey
	lda (lowtr),y
	bcs fndlnc
flinrt	clc
flnrts	rts
scrath	bne flnrts
scrtch	lda #0
	tay
	sta (txttab),y
	iny
	sta (txttab),y
	lda txttab
	clc
	adc #2
	sta vartab
	lda txttab+1
	adc #0
	sta vartab+1
runc	jsr stxtpt
	lda #0
clear	bne stkrts
clearc	jsr ccall       ;moved for v2 orig for rs-232
cleart	lda memsiz      ;entry for open & close memsiz changes
	ldy memsiz+1
	sta fretop
	sty fretop+1
	lda vartab
	ldy vartab+1
	sta arytab
	sty arytab+1
	sta strend
	sty strend+1
fload	jsr restor
stkini	ldx #tempst
	stx temppt
	pla 
	tay
	pla
	ldx #stkend-257
	txs
	pha
	tya
	pha
	lda #0
	sta oldtxt+1
	sta subflg
stkrts	rts
stxtpt	clc
	lda txttab
	adc #255
	sta txtptr
	lda txttab+1
	adc #255
	sta txtptr+1
	rts

