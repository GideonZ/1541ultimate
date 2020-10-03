	sta index1
	stx index1+1
svar	cpx arytab+1
	bne svargo
	cmp arytab
	beq aryvar
svargo	jsr dvars
	beq svar
aryvar	sta arypnt
	stx arypnt+1
	lda #strsiz
	sta four6
aryva2	lda arypnt
	ldx arypnt+1
aryva3	cpx strend+1
	bne aryvgo
	cmp strend
	bne *+5
	jmp grbpas
aryvgo	sta index1
	stx index1+1
	ldy #1-addprc
	lda (index1),y
	tax
	iny
	lda (index1),y
	php
	iny
	lda (index1),y
	adc arypnt
	sta arypnt
	iny
	lda (index1),y
	adc arypnt+1
	sta arypnt+1
	plp
	bpl aryva2
	txa
	bmi aryva2
	iny
	lda (index1),y
	ldy #0
	asl a
	adc #5
	adc index1
	sta index1
	bcc aryget
	inc index1+1
aryget	ldx index1+1
arystr	cpx arypnt+1
	bne gogo
	cmp arypnt
	beq aryva3
gogo	jsr dvar
	beq arystr
dvars	lda (index1),y
	bmi dvarts
	iny
	lda (index1),y
	bpl dvarts
	iny
dvar	lda (index1),y
	beq dvarts
	iny
	lda (index1),y
	tax
	iny
	lda (index1),y
	cmp fretop+1
	bcc dvar2
	bne dvarts
	cpx fretop
	bcs dvarts
dvar2	cmp grbtop+1
	bcc dvarts
	bne dvar3
	cpx grbtop
	bcc dvarts
dvar3	stx grbtop
	sta grbtop+1
	lda index1
	ldx index1+1
	sta grbpnt
	stx grbpnt+1
	lda four6
	sta size
dvarts	lda four6
	clc
	adc index1
	sta index1
	bcc grbrts
	inc index1+1
grbrts	ldx index1+1
	ldy #0
	rts
grbpas	lda grbpnt+1
	ora grbpnt
	beq grbrts
	lda size
	and #4
	lsr a
	tay
	sta size
	lda (grbpnt),y
	adc lowtr
	sta hightr
	lda lowtr+1
	adc #0
	sta hightr+1
	lda fretop
	ldx fretop+1
	sta highds
	stx highds+1
	jsr bltuc
	ldy size
	iny
	lda highds
	sta (grbpnt),y
	tax
	inc highds+1
	lda highds+1
	iny
	sta (grbpnt),y
	jmp fndvar
cat	lda faclo
	pha
	lda facmo
	pha
	jsr eval
	jsr chkstr
	pla
	sta strng1
	pla
	sta strng1+1
	ldy #0
	lda (strng1),y
	clc
	adc (facmo),y
	bcc sizeok
	ldx #errls
	jmp error
sizeok	jsr strini
	jsr movins
	lda dscpnt
	ldy dscpnt+1
	jsr fretmp
	jsr movdo
	lda strng1
	ldy strng1+1
	jsr fretmp
	jsr putnew
	jmp tstop
movins	ldy #0
	lda (strng1),y
	pha
	iny

