omerr	ldx #errom
error	jmp (ierror)
nerrox	txa
	asl a
	tax
	lda errtab-2,x
	sta index1
	lda errtab-1,x
	sta index1+1
	jsr clschn
	lda #0
	sta channl
errcrd	jsr crdo
	jsr outqst
	ldy #0
geterr	lda (index1),y
	pha
	and #127
	jsr outdo
	iny
	pla
	bpl geterr
	jsr stkini
	lda #<err
	ldy #>err
errfin	jsr strout
	ldy curlin+1
	iny
	beq readyx
	jsr inprt

readyx	lda #<reddy
	ldy #>reddy
	jsr strout
	lda #$80        ;direct messages on
	jsr setmsg      ;from kernal

main	jmp (imain)
nmain	jsr inlin
	stx txtptr
	sty txtptr+1
	jsr chrget
	tax
	beq main
	ldx #255
	stx curlin+1
	bcc main1
	jsr crunch
	jmp gone
main1	jsr linget
	jsr crunch
	sty count
	jsr fndlin
	bcc nodel
	ldy #1
	lda (lowtr),y
	sta index1+1
	lda vartab
	sta index1
	lda lowtr+1
	sta index2+1
	lda lowtr
	dey
	sbc (lowtr),y 
	clc
	adc vartab
	sta vartab
	sta index2
	lda vartab+1
	adc #255
	sta vartab+1
	sbc lowtr+1
	tax
	sec
	lda lowtr
	sbc vartab
	tay
	bcs qdect1
	inx
	dec index2+1
qdect1	clc
	adc index1
	bcc mloop
	dec index1+1
	clc
mloop	lda (index1),y
	sta (index2),y
	iny 
	bne mloop
	inc index1+1
	inc index2+1
	dex
	bne mloop
nodel	jsr runc
	jsr lnkprg
	lda buf
	beq main
	clc
	lda vartab
	sta hightr 
	adc count
	sta highds
	ldy vartab+1
	sty hightr+1
	bcc nodelc
	iny
nodelc	sty highds+1
	jsr bltu
	lda linnum
	ldy linnum+1
	sta buf-2
	sty buf-1
	lda strend
	ldy strend+1
	sta vartab
	sty vartab+1
	ldy count
	dey
stolop	lda buf-4,y
	sta (lowtr),y
	dey
	bpl stolop
fini	jsr runc
	jsr lnkprg
	jmp main
lnkprg	lda txttab
	ldy txttab+1
	sta index
	sty index+1
	clc 
chead	ldy #1
	lda (index),y
	beq lnkrts
	ldy #4
czloop	iny
	lda (index),y
	bne czloop
	iny
	tya
	adc index
	tax
	ldy #0
	sta (index),y
	lda index+1
	adc #0
	iny
	sta (index),y
	stx index
	sta index+1
	bcc chead
lnkrts	rts

;function to get a line one character at
;a time from the input channel and
;build it in the input buffer.
;
inlin	ldx #0
;
inlinc	jsr inchr
	cmp #13         ;a carriage return?
	beq finin1      ;yes...done build
;
	sta buf,x       ;put it away
	inx
	cpx #buflen     ;max character line?
	bcc inlinc      ;no...o.k.
;
	ldx #errls      ;string too long error
	jmp error
;
finin1	jmp fininl

