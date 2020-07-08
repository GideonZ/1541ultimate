;**********************************
;* load ram function              *
;*                                *
;* loads from cassette 1 or 2, or *
;* serial bus devices >=4 to 31   *
;* as determined by contents of   *
;* variable fa. verify flag in .a *
;*                                *
;* alt load if sa=0, normal sa=1  *
;* .x , .y load address if sa=0   *
;* .a=0 performs load,<> is verify*
;*                                *
;* high load return in x,y.       *
;*                                *
;**********************************

loadsp	stx memuss      ;.x has low alt start
	sty memuss+1
load	jmp (iload)     ;monitor load entry
;
nload	sta verck       ;store verify flag
	lda #0
	sta status
;
	lda fa          ;check device number
	bne ld20
;
ld10	jmp error9      ;bad device #-keyboard
;
ld20	cmp #3
	beq ld10        ;disallow screen load
	bcc ld100       ;handle tapes different
;
;load from cbm ieee device
;
	ldy fnlen       ;must have file name
	bne ld25        ;yes...ok
;
	jmp error8      ;missing file name
;
ld25	ldx sa          ;save sa in .x
	jsr luking      ;tell user looking
	lda #$60        ;special load command
	sta sa
	jsr openi       ;open the file
;
	lda fa
	jsr talk        ;establish the channel
	lda sa
	jsr tksa        ;tell it to load
;
	jsr acptr       ;get first byte
	sta eal
;
	lda status      ;test status for error
	lsr a
	lsr a
	bcs ld90        ;file not found...
	jsr acptr
	sta eah
;
	txa             ;find out old sa
	bne ld30        ;sa<>0 use disk address
	lda memuss      ;else load where user wants
	sta eal
	lda memuss+1
	sta eah
ld30	jsr loding      ;tell user loading
;
ld40	lda #$fd        ;mask off timeout
	and status
	sta status
;
	jsr stop        ;stop key?
	bne ld45        ;no...
;
	jmp break       ;stop key pressed
;
ld45	jsr acptr       ;get byte off ieee
	tax
	lda status      ;was there a timeout?
	lsr a
	lsr a
	bcs ld40        ;yes...try again
	txa
	ldy verck       ;performing verify?
	beq ld50        ;no...load
	ldy #0
	cmp (eal),y      ;verify it
	beq ld60        ;o.k....
	lda #sperr      ;no good...verify error
	jsr udst        ;update status
	.byt $2c        ;skip next store
;
ld50	sta (eal),y
ld60	inc eal         ;increment store addr
	bne ld64
	inc eah
ld64	bit status      ;eoi?
	bvc ld40        ;no...continue load
;
	jsr untlk       ;close channel
	jsr clsei       ;close the file
	bcc ld180       ;branch always
;
ld90	jmp error4      ;file not found
;
;load from tape
;
ld100	lsr a
	bcs ld102       ;if c-set then it's cassette
;
	jmp error9      ;bad device #
;
ld102	jsr zzz         ;set pointers at tape
	bcs ld104
	jmp error9      ;deallocated...
ld104	jsr cste1       ;tell user about buttons
	bcs ld190       ;stop key pressed?
	jsr luking      ;tell user searching
;
ld112	lda fnlen       ;is there a name?
	beq ld150       ;none...load anything
	jsr faf         ;find a file on tape
	bcc ld170       ;got it!
	beq ld190       ;stop key pressed
	bcs ld90        ;nope...end of tape
;
ld150	jsr fah         ;find any header
	beq ld190       ;stop key pressed
	bcs ld90        ;no header
;
ld170	lda status
	and #sperr      ;must got header right
	sec
	bne ld190       ;is bad
;
	cpx #blf        ;is it a movable program...
	beq ld178       ;yes
;
	cpx #plf        ;is it a program
	bne ld112       ;no...its something else
;
ld177	ldy #1          ;fixed load...
	lda (tape1),y    ;...the address in the...
	sta memuss      ;...buffer is the start address
	iny
	lda (tape1),y
	sta memuss+1
	bcs ld179       ;jmp ..carry set by cpx's
;
ld178	lda sa          ;check for monitor load...
	bne ld177       ;...yes we want fixed type
;
ld179	ldy #3          ;tapea - tapesta
;carry set by cpx's
	lda (tape1),y
	ldy #1
	sbc (tape1),y
	tax             ;low to .x
	ldy #4
	lda (tape1),y
	ldy #2
	sbc (tape1),y
	tay             ;high to .y
;
	clc             ;ea = sta+(tapea-tapesta)
	txa
	adc memuss      ;
	sta eal
	tya
	adc memuss+1
	sta eah
	lda memuss      ;set up starting address
	sta stal
	lda memuss+1
	sta stah
	jsr loding      ;tell user loading
	jsr trd         ;do tape block load
	.byt $24        ;carry from trd
;
ld180	clc             ;good exit
;
; set up end load address
;
	ldx eal
	ldy eah
;
ld190	rts

;subroutine to print to console:
;
;searching [for name]
;
luking	lda msgflg      ;supposed to print?
	bpl ld115       ;...no
	ldy #ms5-ms1    ;"searching"
	jsr msg
	lda fnlen
	beq ld115
	ldy #ms6-ms1    ;"for"
	jsr msg

;subroutine to output file name
;
outfn	ldy fnlen       ;is there a name?
	beq ld115       ;no...done
	ldy #0
ld110	lda (fnadr),y
	jsr bsout
	iny
	cpy fnlen
	bne ld110
;
ld115	rts

;subroutine to print:
;
;loading/verifing
;
loding	ldy #ms10-ms1   ;assume 'loading'
	lda verck       ;check flag
	beq ld410       ;are doing load
	ldy #ms21-ms1   ;are 'verifying'
ld410	jmp spmsg

