	ldy curtol
	beq deccur
zerita	dey
	sta (arypnt),y
	bne zerita
deccur	dec arypnt+1
	dec curtol+1
	bne zerita
	inc arypnt+1
	sec
	lda strend
	sbc lowtr
	ldy #2
	sta (lowtr),y
	lda strend+1
	iny
	sbc lowtr+1
	sta (lowtr),y
	lda dimflg
	bne dimrts
	iny
getdef	lda (lowtr),y
	sta count
	lda #0
	sta curtol
inlpnm	sta curtol+1
	iny
	pla
	tax
	sta indice
	pla
	sta indice+1
	cmp (lowtr),y
	bcc inlpn2
	bne bserr7
	iny
	txa
	cmp (lowtr),y
	bcc inlpn1
bserr7	jmp bserr
omerr1	jmp omerr
inlpn2	iny
inlpn1	lda curtol+1
	ora curtol
	clc
	beq addind
	jsr umult
	txa
	adc indice
	tax
	tya
	ldy index1
addind	adc indice+1
	stx curtol
	dec count
	bne inlpnm
	sta curtol+1
	ldx #5
	lda varnam
	bpl notfl1
	dex
notfl1	lda varnam+1
	bpl stoml1
	dex
	dex
stoml1	stx addend
	lda #0
	jsr umultd
	txa
	adc arypnt
	sta varpnt
	tya
	adc arypnt+1
	sta varpnt+1
	tay
	lda varpnt
dimrts	rts
umult	sty index
	lda (lowtr),y
	sta addend
	dey
	lda (lowtr),y
umultd	sta addend+1
	lda #16
	sta deccnt
	ldx #0
	ldy #0
umultc	txa
	asl a
	tax
	tya
	rol a
	tay
	bcs omerr1
	asl curtol
	rol curtol+1
	bcc umlcnt
	clc
	txa
	adc addend
	tax
	tya
	adc addend+1
	tay
	bcs omerr1
umlcnt	dec deccnt
	bne umultc
umlrts	rts
fre	lda valtyp
	beq nofref
	jsr frefac
nofref	jsr garba2
	sec
	lda fretop
	sbc strend
	tay
	lda fretop+1
	sbc strend+1
givayf	ldx #0
	stx valtyp
	sta facho
	sty facho+1
	ldx #144
	jmp floats
pos	sec
	jsr plot        ;get tab pos in .y
sngflt	lda #0
	beq givayf
errdir	ldx curlin+1
	inx
	bne dimrts
	ldx #errid
	.byt $2c
errguf	ldx #erruf
	jmp error
def	jsr getfnm
	jsr errdir
	jsr chkopn
	lda #128
	sta subflg
	jsr ptrget
	jsr chknum
	jsr chkcls
	lda #$b2
	jsr synchr
	pha
	lda varpnt+1
	pha
	lda varpnt
	pha
	lda txtptr+1

