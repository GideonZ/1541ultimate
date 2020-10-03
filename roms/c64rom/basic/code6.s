timelp	sty fbufpt
	jsr timnum
	jsr mul10
	inc fbufpt
	ldy fbufpt
	jsr timnum
	jsr movaf
	tax
	beq noml6
	inx
	txa
	jsr finml6
noml6	ldy fbufpt
	iny
	cpy #6
	bne timelp
	jsr mul10
	jsr qint
	ldx facmo
	ldy facmoh
	lda faclo
	jmp settim
timnum	lda (index),y
	jsr qnum
	bcc gotnum
fcerr2	jmp fcerr
gotnum	sbc #$2f
	jmp finlog
getspt	ldy #2
	lda (facmo),y
	cmp fretop+1
	bcc dntcpy
	bne qvaria
	dey
	lda (facmo),y
	cmp fretop
	bcc dntcpy
qvaria	ldy faclo
	cpy vartab+1
	bcc dntcpy
	bne copy
	lda facmo
	cmp vartab
	bcs copy
dntcpy	lda facmo
	ldy facmo+1
	jmp copyc
copy	ldy #0
	lda (facmo),y
	jsr strini
	lda dscpnt
	ldy dscpnt+1
	sta strng1
	sty strng1+1
	jsr movins
	lda #<dsctmp
	ldy #>dsctmp
copyc	sta dscpnt
	sty dscpnt+1
	jsr fretms
	ldy #0
	lda (dscpnt),y
	sta (forpnt),y
	iny
	lda (dscpnt),y
	sta (forpnt),y
	iny
	lda (dscpnt),y
	sta (forpnt),y
	rts
printn	jsr cmd
	jmp iodone
cmd	jsr getbyt
	beq saveit
	lda #44
	jsr synchr
saveit	php
	stx channl
	jsr coout
	plp
	jmp print
strdon	jsr strprt
newchr	jsr chrgot
print	beq crdo
printc	beq prtrts
	cmp #tabtk
	beq taber
	cmp #spctk
	clc
	beq taber
	cmp #44
	beq comprt
	cmp #59
	beq notabr
	jsr frmevl
	bit valtyp
	bmi strdon
	jsr fout
	jsr strlit
	jsr strprt
	jsr outspc
	bne newchr
fininl	lda #0
	sta buf,x
zz5=buf-1
	ldx #<zz5
	ldy #>zz5
	lda channl
	bne prtrts
crdo	lda #13
	jsr outdo
	bit channl
	bpl crfin
;
	lda #10
	jsr outdo
crfin	eor #255
prtrts	rts
comprt	sec
	jsr plot        ;get tab position in x
	tya
ncmpos	=$1e
	sec
morco1	sbc #clmwid
	bcs morco1
	eor #255
	adc #1
	bne aspac
taber	php
	sec
	jsr plot        ;read tab position
	sty trmpos
	jsr gtbytc
	cmp #41
	bne snerr4
	plp
	bcc xspac
	txa
	sbc trmpos
	bcc notabr
aspac	tax
xspac	inx
xspac2	dex
	bne xspac1
notabr	jsr chrget
	jmp printc

