	.segment "TAPE"
;fah -- find any header
;
;reads tape device until one of following
;block types found: bdfh--basic data
;file header, blf--basic load file
;for success carry is clear on return.
;for failure carry is set on return.
;in addition accumulator is 0 if stop
;key was pressed.
;
fah	lda verck       ;save old verify
	pha
	jsr rblk        ;read tape block
	pla
	sta verck       ;restore verify flag
	bcs fah40       ;read terminated
;
	ldy #0
	lda (tape1),y    ;get header type
;
	cmp #eot        ;check end of tape?
	beq fah40       ;yes...failure
;
	cmp #blf        ;basic load file?
	beq fah50       ;yes...success
;
	cmp #plf        ;fixed load file?
	beq fah50       ;yes...success
;
	cmp #bdfh       ;basic data file?
	bne fah         ;no...keep trying
;
fah50	tax             ;return file type in .x
	bit msgflg      ;printing messages?
	bpl fah45       ;no...
;
	ldy #ms17-ms1   ;print "found"
	jsr msg
;
;output complete file name
;
	ldy #5
fah55	lda (tape1),y
	jsr bsout
	iny
	cpy #21
	bne fah55
;
fah56	lda time+1      ;set up for time out...
	jsr fpatch      ;goto patch...
	nop
;
fah45	clc             ;success flag
	dey             ;make nonzero for okay return
;
fah40	rts

;tapeh--write tape header
;error if tape buffer de-allocated
;carry clear if o.k.
;
tapeh	sta t1
;
;determine address of buffer
;
	jsr zzz
	bcc th40        ;buffer was de-allocated
;
;preserve start and end addresses
;for case of header for load file
;
	lda stah
	pha
	lda stal
	pha
	lda eah
	pha
	lda eal
	pha
;
;put blanks in tape buffer
;
	ldy #bufsz-1
	lda #' '
blnk2	sta (tape1),y
	dey
	bne blnk2
;
;put block type in header
;
	lda t1
	sta (tape1),y
;
;put start load address in header
;
	iny
	lda stal
	sta (tape1),y
	iny
	lda stah
	sta (tape1),y
;
;put end load address in header
;
	iny
	lda eal
	sta (tape1),y
	iny
	lda eah
	sta (tape1),y
;
;put file name in header
;
	iny
	sty t2
	ldy #0
	sty t1
th20	ldy t1
	cpy fnlen
	beq th30
	lda (fnadr),y
	ldy t2
	sta (tape1),y
	inc t1
	inc t2
	bne th20
;
;set up start and end address of header
;
th30	jsr ldad1
;
;set up time for leader
;
	lda #$69
	sta shcnh
;
	jsr twrt2       ;write header on tape
;
;restore start and end address of
;load file.
;
	tay             ;save error code in .y
	pla
	sta eal
	pla 
	sta eah
	pla
	sta stal
	pla
	sta stah
	tya             ;restore error code for return
;
th40	rts

;function to return tape buffer
;address in tape1
;
zzz	ldx tape1       ;assume tape1
	ldy tape1+1
	cpy #>buf       ;check for allocation...
;...[tape1+1]=0 or 1 means deallocated
;...c clr => deallocated
	rts

ldad1	jsr zzz         ;get ptr to cassette
	txa
	sta stal        ;save start low
	clc
	adc #bufsz      ;compute pointer to end
	sta eal         ;save end low
	tya
	sta stah        ;save start high
	adc #0          ;compute pointer to end
	sta eah         ;save end high
	rts

faf	jsr fah         ;find any header
	bcs faf40       ;failed
;
;success...see if right name
;
	ldy #5          ;offset into tape header
	sty t2
	ldy #0          ;offset into file name
	sty t1
faf20	cpy fnlen       ;compare this many
	beq faf30       ;done
;
	lda (fnadr),y
	ldy t2
	cmp (tape1),y
	bne faf         ;mismatch--try next header
	inc t1
	inc t2
	ldy t1
	bne faf20       ;branch always
;
faf30	clc             ;success flag
faf40	rts

; rsr  4/10/82 add key down test in fah...
