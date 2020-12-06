;test pointer to variable to see
;if constant is contained in basic.
;array variables have zeroes placed
;in ram. undefined simple variables
;have pointer t zero in basic.
;
tstrom	sec
	lda facmo
	sbc #<romloc
	lda faclo
	sbc #>romloc
	bcc tstr10
;
	lda #<initat
	sbc facmo
	lda #>initat
	sbc faclo
;
tstr10	rts

isvar	jsr ptrget
isvret	sta facmo
	sty facmo+1
	ldx varnam
	ldy varnam+1
	lda valtyp
	beq gooo
	lda #0
	sta facov
	jsr tstrom      ;see if an array
	bcc strrts      ;don't test st(i),ti(i)
	cpx #'T'
	bne strrts
	cpy #$c9
	bne strrts
	jsr gettim
	sty tenexp
	dey
	sty fbufpt
	ldy #6
	sty deccnt
	ldy #fdcend-foutbl
	jsr foutim
	jmp timstr
strrts	rts
gooo	bit intflg
	bpl gooooo
	ldy #0
	lda (facmo),y
	tax
	iny
	lda (facmo),y
	tay
	txa
	jmp givayf
gooooo	jsr tstrom      ;see if array
	bcc gomovf      ;don't test st(i),ti(i)
	cpx #'T'
	bne qstatv
	cpy #'I'
	bne gomovf
	jsr gettim
	tya
	ldx #160
	jmp floatb
gettim	jsr rdtim
	stx facmo
	sty facmoh
	sta faclo
	ldy #0
	sty facho
	rts
qstatv	cpx #'S'
	bne gomovf
	cpy #'T'
	bne gomovf
	jsr readst
	jmp float
gomovf	lda facmo
	ldy facmo+1
	jmp movfm
isfun	asl a
	pha
	tax
	jsr chrget
	cpx #lasnum+lasnum-255
	bcc oknorm
	jsr chkopn
	jsr frmevl
	jsr chkcom
	jsr chkstr
	pla
	tax
	lda facmo+1
	pha
	lda facmo
	pha
	txa
	pha
	jsr getbyt
	pla
	tay
	txa
	pha
	jmp fingo
oknorm	jsr parchk
	pla
	tay
fingo	lda fundsp-onefun-onefun+256,y
	sta jmper+1
	lda fundsp-onefun-onefun+257,y
	sta jmper+2
	jsr jmper
	jmp chknum
orop	ldy #255
	.byt $2c
andop	ldy #0
	sty count
	jsr ayint
	lda facmo
	eor count
	sta integr
	lda faclo
	eor count
	sta integr+1
	jsr movfa
	jsr ayint
	lda faclo
	eor count
	and integr+1
	eor count
	tay
	lda facmo
	eor count
	and integr
	eor count
	jmp givayf
dorel	jsr chkval
	bcs strcmp
	lda argsgn
	ora #127
	and argho
	sta argho
	lda #<argexp
	ldy #>argexp
	jsr fcomp
	tax
	jmp qcomp
strcmp	lda #0
	sta valtyp
	dec opmask
	jsr frefac
	sta dsctmp
	stx dsctmp+1
	sty dsctmp+2
	lda argmo
	ldy argmo+1
	jsr fretmp
	stx argmo
	sty argmo+1
	tax
	sec
	sbc dsctmp
	beq stasgn
	lda #1
	bcc stasgn
	ldx dsctmp
	lda #$ff
stasgn	sta facsgn
	ldy #255
	inx
nxtcmp	iny
	dex
	bne getcmp
	ldx facsgn
qcomp	bmi docmp
	clc
	bcc docmp
getcmp	lda (argmo),y

