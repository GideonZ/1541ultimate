	stx facmoh
	ldx facmo+1
	stx facmo
	ldx facov
	stx faclo
	sty facov
	adc #$08
addpr2	=addprc+addprc
addpr4	=addpr2+addpr2
addpr8	=addpr4+addpr4
	cmp #$18+addpr8
	bne norm3
zerofc	lda #0
zerof1	sta facexp
zeroml	sta facsgn
	rts
fadd2	adc oldov
	sta facov
	lda faclo
	adc arglo
	sta faclo
	lda facmo
	adc argmo
	sta facmo
	lda facmoh
	adc argmoh
	sta facmoh
	lda facho
	adc argho
	sta facho
	jmp squeez
norm2	adc #1
	asl facov
	rol faclo
	rol facmo
	rol facmoh
	rol facho
norm1	bpl norm2
	sec
	sbc facexp
	bcs zerofc
	eor #$ff
	adc #1
	sta facexp
squeez	bcc rndrts
rndshf	inc facexp
	beq overr
	ror facho
	ror facmoh
	ror facmo
	ror faclo
	ror facov
rndrts	rts
negfac	lda facsgn
	eor #$ff
	sta facsgn
negfch	lda facho
	eor #$ff
	sta facho
	lda facmoh
	eor #$ff
	sta facmoh
	lda facmo
	eor #$ff
	sta facmo
	lda faclo
	eor #$ff
	sta faclo
	lda facov
	eor #$ff
	sta facov
	inc facov
	bne incfrt
incfac	inc faclo
	bne incfrt
	inc facmo
	bne incfrt
	inc facmoh
	bne incfrt
	inc facho
incfrt	rts
overr	ldx #errov
	jmp error
mulshf	ldx #resho-1
shftr2	ldy 3+addprc,x
	sty facov
	ldy 3,x
	sty 4,x
	ldy 2,x
	sty 3,x
	ldy 1,x
	sty 2,x
	ldy bits
	sty 1,x
shiftr	adc #$08
	bmi shftr2
	beq shftr2
	sbc #$08
	tay
	lda facov
	bcs shftrt
shftr3	asl 1,x
	bcc shftr4
	inc 1,x
shftr4	ror 1,x
	ror 1,x
rolshf	ror 2,x
	ror 3,x
	ror 4,x
	ror a
	iny
	bne shftr3
shftrt	clc
	rts
fone	.byt $81,$00,$00,$00,$00
logcn2	.byt $03,$7f,$5e,$56
	.byt $cb,$79,$80,$13
	.byt $9b,$0b,$64,$80
	.byt $76,$38,$93,$16
	.byt $82,$38,$aa,$3b,$20
sqr05	.byt $80,$35,$04,$f3,$34
sqr20	.byt $81,$35,$04,$f3,$34
neghlf	.byt $80,$80,$00,$00,$00
log2	.byt $80,$31,$72,$17,$f8
