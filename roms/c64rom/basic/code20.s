movfr	lda resho
	sta facho
	lda resmoh
	sta facmoh
	lda resmo
	sta facmo
	lda reslo
	sta faclo
	jmp normal
movfm	sta index1
	sty index1+1
	ldy #3+addprc
	lda (index1),y
	sta faclo
	dey
	lda (index1),y
	sta facmo
	dey
	lda (index1),y
	sta facmoh
	dey
	lda (index1),y
	sta facsgn
	ora #$80
	sta facho
	dey
	lda (index1),y
	sta facexp
	sty facov
	rts   
mov2f	ldx #tempf2
	.byt $2c
mov1f	ldx #tempf1
	ldy #0
	beq movmf
movvf	ldx forpnt
	ldy forpnt+1
movmf	jsr round
	stx index1
	sty index1+1
	ldy #3+addprc
	lda faclo
	sta (index),y
	dey
	lda facmo
	sta (index),y
	dey
	lda facmoh
	sta (index),y
	dey
	lda facsgn
	ora #$7f
	and facho
	sta (index),y
	dey
	lda facexp
	sta (index),y
	sty facov
	rts
movfa	lda argsgn
movfa1	sta facsgn
	ldx #4+addprc
movfal	lda argexp-1,x
	sta facexp-1,x
	dex
	bne movfal
	stx facov
	rts
movaf	jsr round
movef	ldx #5+addprc
movafl	lda facexp-1,x
	sta argexp-1,x
	dex
	bne movafl
	stx facov
movrts	rts
round	lda facexp
	beq movrts
	asl facov
	bcc movrts
incrnd	jsr incfac
	bne movrts
	jmp rndshf
sign	lda facexp
	beq signrt
fcsign	lda facsgn
fcomps	rol a
	lda #$ff
	bcs signrt
	lda #1
signrt	rts
sgn	jsr sign
float	sta facho
	lda #0
	sta facho+1
	ldx #$88
floats	lda facho
	eor #$ff
	rol a
floatc	lda #0
	sta faclo
	sta facmo
floatb	stx facexp
	sta facov
	sta facsgn
	jmp fadflt
abs	lsr facsgn
	rts
fcomp	sta index2
fcompn	sty index2+1
	ldy #0
	lda (index2),y
	iny
	tax
	beq sign
	lda (index2),y
	eor facsgn
	bmi fcsign
	cpx facexp
	bne fcompc
	lda (index2),y
	ora #$80
	cmp facho
	bne fcompc
	iny
	lda (index2),y
	cmp facmoh
	bne fcompc
	iny
	lda (index2),y
	cmp facmo
	bne fcompc
	iny
	lda #$7f
	cmp facov
	lda (index2),y
	sbc faclo
	beq qintrt
fcompc	lda facsgn
	bcc fcompd
	eor #$ff
fcompd	jmp fcomps

