	lda #'-'
fout14	sta fbuffr+1,y
	lda #'E'
	sta fbuffr,y
	txa
	ldx #$2f
	sec
fout15	inx
	sbc #$0a
	bcs fout15
	adc #$3a
	sta fbuffr+3,y
	txa
	sta fbuffr+2,y
	lda #0
	sta fbuffr+4,y
	beq fout20
fout19	sta fbuffr-1,y
fout17	lda #0
	sta fbuffr,y
fout20	lda #<fbuffr
	ldy #>fbuffr
	rts
fhalf	.byt $80,$00
zero	.byt $00,$00,$00
foutbl	.byt $fa,$0a,$1f,$00,$00
	.byt $98,$96,$80,$ff
	.byt $f0,$bd,$c0,$00
	.byt $01,$86,$a0,$ff
	.byt $ff,$d8,$f0,$00,$00
	.byt $03,$e8,$ff,$ff
	.byt $ff,$9c,$00,$00,$00,$0a
	.byt $ff,$ff,$ff,$ff
fdcend	.byt $ff,$df,$0a,$80
	.byt $00,$03,$4b,$c0,$ff
	.byt $ff,$73,$60,$00,$00
	.byt $0e,$10,$ff,$ff
	.byt $fd,$a8,$00,$00,$00,$3c
timend
;
cksma0	.byt $00        ;$a000 8k room check sum adj
patchs	.res 30          ; patch area
;
sqr	jsr movaf
	lda #<fhalf
	ldy #>fhalf
	jsr movfm
fpwrt	beq exp
	lda argexp
	bne fpwrt1
	jmp zerof1
fpwrt1	ldx #<tempf3
	ldy #>tempf3
	jsr movmf
	lda argsgn
	bpl fpwr1
	jsr int
	lda #<tempf3
	ldy #>tempf3
	jsr fcomp
	bne fpwr1
	tya
	ldy integr
fpwr1	jsr movfa1
	tya
	pha
	jsr log
	lda #<tempf3
	ldy #>tempf3
	jsr fmult
	jsr exp
	pla
	lsr a
	bcc negrts
negop	lda facexp
	beq negrts
	lda facsgn
	eor #$ff
	sta facsgn
negrts	rts

