	.segment "HIBASIC"         ;start of vic-40 kernal rom

; continuation of exponent routine
;
stold	sta oldov
	jsr movef
	lda facexp
	cmp #$88
	bcc exp1
gomldv	jsr mldvex
exp1	jsr int
	lda integr
	clc
	adc #$81
	beq gomldv
	sec
	sbc #1
	pha
	ldx #4+addprc
swaplp	lda argexp,x
	ldy facexp,x
	sta facexp,x
	sty argexp,x
	dex
	bpl swaplp
	lda oldov
	sta facov
	jsr fsubt
	jsr negop
	lda #<expcon
	ldy #>expcon
	jsr poly
	lda #0
	sta arisgn
	pla
	jsr mldexp
	rts
polyx	sta polypt
	sty polypt+1
	jsr mov1f
	lda #tempf1
	jsr fmult
	jsr poly1
	lda #<tempf1
	ldy #>tempf1
	jmp fmult
poly	sta polypt
	sty polypt+1
poly1	jsr mov2f
	lda (polypt),y
	sta degree
	ldy polypt
	iny
	tya
	bne poly3
	inc polypt+1
poly3	sta polypt
	ldy polypt+1
poly2	jsr fmult
	lda polypt
	ldy polypt+1
	clc
	adc #4+addprc
	bcc poly4
	iny
poly4	sta polypt
	sty polypt+1
	jsr fadd
	lda #<tempf2
	ldy #>tempf2
	dec degree
	bne poly2
	rts
rmulc	.byt $98,$35,$44,$7a,$00
raddc	.byt $68,$28,$b1,$46,$00
rnd	jsr sign
	bmi rnd1
	bne qsetnr
	jsr rdbas
	stx index1
	sty index1+1
	ldy #4
	lda (index1),y
	sta facho
	iny
	lda (index1),y
	sta facmo
	ldy #8
	lda (index1),y
	sta facmoh
	iny
	lda (index1),y
	sta faclo
	jmp strnex
qsetnr	lda #<rndx
	ldy #>rndx
	jsr movfm
	lda #<rmulc
	ldy #>rmulc
	jsr fmult
	lda #<raddc
	ldy #>raddc
	jsr fadd
rnd1	ldx faclo
	lda facho
	sta faclo
	stx facho
	ldx facmoh
	lda facmo
	sta facmoh
	stx facmo
strnex	lda #0
	sta facsgn
	lda facexp
	sta facov
	lda #$80
	sta facexp
	jsr normal
	ldx #<rndx
	ldy #>rndx
gmovmf	jmp movmf

