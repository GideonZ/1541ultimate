xspac1	jsr outspc
	bne xspac2
strout	jsr strlit
strprt	jsr frefac
	tax
	ldy #0
	inx
strpr2	dex
	beq prtrts
	lda (index),y
	jsr outdo
	iny
	cmp #13
	bne strpr2
	jsr crfin
	jmp strpr2
outspc
	lda channl
	beq crtskp
	lda #' '
	.byt $2c
crtskp	lda #29
	.byt $2c
outqst	lda #'?'
outdo	jsr outch
outrts	and #255
	rts
trmnok	lda inpflg
	beq trmno1
	bmi getdtl
	ldy #255
	bne stcurl
getdtl	lda datlin
	ldy datlin+1
stcurl	sta curlin
	sty curlin+1
snerr4	jmp snerr
trmno1	lda channl
	beq doagin
	ldx #errbd
	jmp error
doagin	lda #<tryagn
	ldy #>tryagn
	jsr strout
	lda oldtxt
	ldy oldtxt+1
	sta txtptr
	sty txtptr+1
	rts
get	jsr errdir
	cmp #'#'
	bne gettty
	jsr chrget
	jsr getbyt
	lda #44
	jsr synchr
	stx channl
	jsr coin
zz2=buf+1
gettty	ldx #<zz2
zz3=buf+2
	ldy #>zz3
	lda #0
	sta buf+1
	lda #64
	jsr inpco1
	ldx channl
	bne iorele
	rts
inputn	jsr getbyt
	lda #44
	jsr synchr
	stx channl
	jsr coin
	jsr notqti
iodone	lda channl
iorele	jsr clschn
	ldx #0
	stx channl
	rts
input	cmp #34
	bne notqti
	jsr strtxt
	lda #59
	jsr synchr
	jsr strprt
notqti	jsr errdir
	lda #44
	sta buf-1
getagn	jsr qinlin
	lda channl
	beq bufful
	jsr readst
	and #2
	beq bufful
	jsr iodone
	jmp data
bufful	lda buf
	bne inpcon
	lda channl
	bne getagn
	jsr datan
	jmp addon
qinlin	lda channl
	bne ginlin
	jsr outqst
	jsr outspc
ginlin	jmp inlin
read	ldx datptr
	ldy datptr+1
	.byt $a9
	tya
	.byt $2c
inpcon	lda #0
inpco1	sta inpflg
	stx inpptr
	sty inpptr+1
inloop	jsr ptrget
	sta forpnt
	sty forpnt+1
	lda txtptr
	ldy txtptr+1
	sta vartxt
	sty vartxt+1
	ldx inpptr
	ldy inpptr+1
	stx txtptr
	sty txtptr+1
	jsr chrgot
	bne datbk1
	bit inpflg
	bvc qdata
	jsr cgetl
	sta buf
zz4=buf-1
	ldx #<zz4
	ldy #>zz4
	bne datbk
qdata	bmi datlop
	lda channl
	bne getnth
	jsr outqst
getnth	jsr qinlin
datbk	stx txtptr
	sty txtptr+1

