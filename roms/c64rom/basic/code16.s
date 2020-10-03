	lda (strng1),y
	tax
	iny
	lda (strng1),y
	tay
	pla
movstr	stx index
	sty index+1
movdo	tay
	beq mvdone
	pha
movlp	dey
	lda (index),y
	sta (frespc),y
	tya
	bne movlp
	pla
mvdone	clc
	adc frespc
	sta frespc
	bcc mvstrt
	inc frespc+1
mvstrt	rts
frestr	jsr chkstr
frefac	lda facmo
	ldy facmo+1
fretmp	sta index
	sty index+1
	jsr fretms
	php
	ldy #0
	lda (index),y
	pha
	iny
	lda (index),y
	tax
	iny
	lda (index),y
	tay
	pla
	plp
	bne fretrt
	cpy fretop+1
	bne fretrt
	cpx fretop
	bne fretrt
	pha
	clc
	adc fretop
	sta fretop
	bcc frepla
	inc fretop+1
frepla	pla
fretrt	stx index
	sty index+1
	rts
fretms	cpy lastpt+1
	bne frerts
	cmp lastpt
	bne frerts
	sta temppt
	sbc #strsiz
	sta lastpt
	ldy #0
frerts	rts
chrd	jsr conint
	txa
	pha
	lda #1
	jsr strspa
	pla
	ldy #0
	sta (dsctmp+1),y
	pla
	pla
	jmp putnew
leftd	jsr pream
	cmp (dscpnt),y
	tya
rleft	bcc rleft1
	lda (dscpnt),y
	tax
	tya
rleft1	pha
rleft2	txa
rleft3	pha
	jsr strspa
	lda dscpnt
	ldy dscpnt+1
	jsr fretmp
	pla
	tay
	pla
	clc
	adc index
	sta index
	bcc pulmor
	inc index+1
pulmor	tya
	jsr movdo
	jmp putnew
rightd	jsr pream
	clc
	sbc (dscpnt),y
	eor #255
	jmp rleft
midd	lda #255
	sta faclo
	jsr chrgot
	cmp #41
	beq mid2
	jsr chkcom
	jsr getbyt
mid2	jsr pream
	beq gofuc
	dex
	txa
	pha
	clc
	ldx #0
	sbc (dscpnt),y
	bcs rleft2
	eor #255
	cmp faclo
	bcc rleft3
	lda faclo
	bcs rleft3
pream	jsr chkcls
	pla
	tay
	pla
	sta jmper+1
	pla
	pla
	pla
	tax
	pla
	sta dscpnt
	pla
	sta dscpnt+1
	lda jmper+1
	pha
	tya
	pha
	ldy #0
	txa
	rts
len	jsr len1
	jmp sngflt
len1	jsr frestr
	ldx #0
	stx valtyp
	tay
	rts
asc	jsr len1
	beq gofuc
	ldy #0
	lda (index1),y
	tay
	jmp sngflt
gofuc	jmp fcerr
gtbytc	jsr chrget

