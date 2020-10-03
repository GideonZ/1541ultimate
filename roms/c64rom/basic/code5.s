	sta txtptr
	lda lowtr+1
	sbc #0
	sta txtptr+1
gorts	rts
return	bne gorts
	lda #255
	sta forpnt+1
	jsr fndfor
	txs
	cmp #gosutk
	beq retu1
	ldx #errrg
	.byt $2c
userr	ldx #errus
	jmp error
snerr2	jmp snerr
retu1	pla
	pla
	sta curlin
	pla
	sta curlin+1
	pla
	sta txtptr
	pla
	sta txtptr+1
data	jsr datan
addon	tya
	clc
	adc txtptr
	sta txtptr
	bcc remrts
	inc txtptr+1
remrts	rts
datan	ldx #':'
	.byt $2c
remn	ldx #0
	stx charac
	ldy #0
	sty endchr
exchqt	lda endchr
	ldx charac
	sta charac
	stx endchr
remer	lda (txtptr),y
	beq remrts
	cmp endchr
	beq remrts
	iny
	cmp #34
	bne remer
	beq exchqt
if	jsr frmevl
	jsr chrgot
	cmp #gototk
	beq okgoto
	lda #thentk
	jsr synchr
okgoto	lda facexp
	bne docond
rem	jsr remn
	beq addon
docond	jsr chrgot
	bcs doco
	jmp goto
doco	jmp gone3
ongoto	jsr getbyt
	pha
	cmp #gosutk
	beq onglop
snerr3	cmp #gototk
	bne snerr2
onglop	dec faclo
	bne onglp1
	pla
	jmp gone2
onglp1	jsr chrget
	jsr linget
	cmp #44
	beq onglop
	pla
ongrts	rts
linget	ldx #0
	stx linnum
	stx linnum+1
morlin	bcs ongrts
	sbc #$2f
	sta charac
	lda linnum+1
	sta index
	cmp #25
	bcs snerr3
	lda linnum
	asl a
	rol index
	asl a
	rol index
	adc linnum
	sta linnum
	lda index
	adc linnum+1
	sta linnum+1
	asl linnum
	rol linnum+1
	lda linnum
	adc charac
	sta linnum
	bcc nxtlgc
	inc linnum+1
nxtlgc	jsr chrget
	jmp morlin
let	jsr ptrget
	sta forpnt
	sty forpnt+1
	lda #$b2
	jsr synchr
	lda intflg
	pha
	lda valtyp
	pha
	jsr frmevl
	pla
	rol a
	jsr chkval
	bne copstr
	pla
qintgr	bpl copflt
	jsr round
	jsr ayint
	ldy #0
	lda facmo
	sta (forpnt),y
	iny
	lda faclo
	sta (forpnt),y
	rts
copflt	jmp movvf
copstr	pla
inpcom	ldy forpnt+1
	cpy #>zero
	bne getspt
	jsr frefac
	cmp #6
	bne fcerr2
	ldy #0
	sty facexp
	sty facsgn

