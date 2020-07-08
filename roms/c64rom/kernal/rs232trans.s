	.segment "RS232"
; rstrab - entry for nmi continue routine
; rstbgn - entry for start transmitter
;
;   rsr - 8/18/80
;
; variables used
;   bitts - # of bits to be sent (<>0 not done)
;   nxtbit - byte contains next bit to be sent
;   roprty - byte contains parity bit calculated
;   rodata - stores data byte currently being transmitted
;   rodbs - output buffer index start
;   rodbe - output buffer index end
;   if rodbs=rodbe then buffer empty
;   robuf - indirect pointer to data buffer
;   rsstat - rs-232 status byte
;
;   xxx us - normal bit path
;   xxx us - worst case parity bit path
;   xxx us - stop bit path
;   xxx us - start bit path
;
rstrab	lda bitts       ;check for place in byte...
	beq rstbgn      ;...done, =0 start next
;
	bmi rst050      ;...doing stop bits
;
	lsr rodata      ;shift data into carry
	ldx #00         ;prepare for a zero
	bcc rst005      ;yes...a zero
	dex             ;no...make an $ff
rst005	txa             ;ready to send
;
	eor roprty      ;calc into parity
	sta roprty
;
	dec bitts       ;bit count down
	beq rst010      ;want a parity instead
;
rstext	txa             ;calc bit whole to send
	and #$04        ;goes out d2pa2
	sta nxtbit
	rts

; calculate parity
;  nxtbit =0 upon entry
;
rst010	lda #$20        ;check 6551 reg bits
	bit m51cdr
	beq rspno       ;...no parity, send a stop
	bmi rst040      ;...not real parity
	bvs rst030      ;...even parity
;
	lda roprty      ;calc odd parity
	bne rspext      ;correct guess
;
rswext	dex             ;wrong guess...its a one
;
rspext	dec bitts       ;one stop bit always
	lda m51ctr      ;check # of stop bits
	bpl rstext      ;...one
	dec bitts       ;...two
	bne rstext      ;jump
;
rspno	;line to send cannot be pb0
	inc bitts       ;counts as one stop bit
	bne rswext      ;jump to flip to one
;
rst030	lda roprty      ;even parity
	beq rspext      ;correct guess...exit
	bne rswext      ;wrong...flip and exit
;
rst040	bvs rspext      ;wanted space
	bvc rswext      ; wanted mark

; stop bits
;
rst050	inc bitts       ;stop bit count towards zero
	ldx #$ff        ;send stop bit
	bne rstext      ;jump to exit
;

; rstbgn - entry to start byte trans
;
rstbgn	lda m51cdr      ;check for 3/x line
	lsr a
	bcc rst060      ;3 line...no check
	bit d2prb       ;check for...
	bpl dsrerr      ;...dsr error
	bvc ctserr      ;...cts error
;
; set up to send next byte
;
rst060	lda #0
	sta roprty      ;zero parity
	sta nxtbit      ;send start bit
	ldx bitnum      ;get # of bits
rst070	stx bitts       ;bitts=#of bitts+1
;
rst080	ldy rodbs       ;check buffer pointers
	cpy rodbe
	beq rsodne      ;all done...
;
	lda (robuf),y    ;get data...
	sta rodata      ;...into byte buffer
	inc rodbs       ;move pointer to next
	rts

; set errors
;
dsrerr	lda #$40        ;dsr gone error
	.byt $2c
ctserr	lda #$10        ;cts gone error
	ora rsstat
	sta rsstat
;
; errors turn off t1
;
rsodne	lda #$01        ;kill t1 nmi
;entry to turn off an enabled nmi...
oenabl	sta d2icr       ;toss bad/old nmi
	eor enabl       ;flip enable
	ora #$80        ;enable good nmi's
	sta enabl
	sta d2icr
	rts

; bitcnt - cal # of bits to be sent
;   returns #of bits+1
;
bitcnt	ldx #9          ;calc word length
	lda #$20
	bit m51ctr
	beq bit010
	dex             ;bit 5 high is a 7 or 5
bit010	bvc bit020
	dex             ;bit 6 high is a 6 or 5
	dex
bit020	rts

; rsr  8/24/80 correct some mistakes
; rsr  8/27/80 change bitnum base to #bits+1
; rsr 12/11/81 modify for vic-40
; rsr  3/11/82 fix enables for bad/old nmi's
