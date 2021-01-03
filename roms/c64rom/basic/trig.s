cos	lda #<pi2
	ldy #>pi2
	jsr fadd
sin	jsr movaf
	lda #<twopi
	ldy #>twopi
	ldx argsgn
	jsr fdivf
	jsr movaf
	jsr int
	lda #0
	sta arisgn
	jsr fsubt
	lda #<fr4
	ldy #>fr4
	jsr fsub
	lda facsgn
	pha
	bpl sin1
	jsr faddh
	lda facsgn
	bmi sin2
	lda tansgn
	eor #$ff
	sta tansgn
sin1	jsr negop
sin2	lda #<fr4
	ldy #>fr4
	jsr fadd
	pla
	bpl sin3
	jsr negop
sin3	lda #<sincon
	ldy #>sincon
	jmp polyx
tan	jsr mov1f
	lda #0
	sta tansgn
	jsr sin
	ldx #<tempf3
	ldy #>tempf3
	jsr gmovmf
	lda #<tempf1
	ldy #>tempf1
	jsr movfm
	lda #0
	sta facsgn
	lda tansgn
	jsr cosc
	lda #<tempf3
	ldy #>tempf3
	jmp fdiv
cosc	pha
	jmp sin1
pi2	.byt $81,$49,$0f,$da,$a2
twopi	.byt $83,$49,$0f,$da,$a2
fr4	.byt $7f,$00,$00,$00,$00
sincon	.byt $05,$84,$e6,$1a,$2d
	.byt $1b,$86,$28,$07,$fb
	.byt $f8,$87,$99,$68,$89
	.byt $01,$87,$23,$35,$df,$e1
	.byt $86,$a5,$5d,$e7,$28,$83
	.byt $49,$f,$da,$a2
atn	lda facsgn
	pha
	bpl atn1
	jsr negop
atn1	lda facexp
	pha
	cmp #$81
	bcc atn2
	lda #<fone
	ldy #>fone
	jsr fdiv
atn2	lda #<atncon
	ldy #>atncon
	jsr polyx
	pla
	cmp #$81
	bcc atn3
	lda #<pi2
	ldy #>pi2
	jsr fsub
atn3	pla
	bpl atn4
	jmp negop
atn4	rts
atncon	.byt $0b,$76,$b3,$83
	.byt $bd,$d3,$79,$1e,$f4
	.byt $a6,$f5,$7b,$83,$fc
	.byt $b0,$10
	.byt $7c,$0c,$1f,$67,$ca
	.byt $7c,$de,$53,$cb,$c1
	.byt $7d,$14,$64,$70,$4c
	.byt $7d,$b7,$ea,$51,$7a
	.byt $7d,$63,$30,$88,$7e
	.byt $7e,$92,$44,$99,$3a
	.byt $7e,$4c,$cc,$91,$c7
	.byt $7f,$aa,$aa,$aa,$13
	.byt $81,0,0,0,0

