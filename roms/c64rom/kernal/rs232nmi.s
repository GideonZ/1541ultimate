	.segment "RS232NMI"
nmi	sei             ;no irq's allowed...
	jmp (nminv)     ;...could mess up cassettes
nnmi	pha
	txa
	pha
	tya
	pha
nnmi10	lda #$7f        ;disable all nmi's
	sta d2icr
	ldy d2icr       ;check if real nmi...
	bmi nnmi20      ;no...rs232/other
;
nnmi18	jsr a0int       ;check if $a0 in...no .y
	bne nnmi19      ;...no
	jmp ($8002)     ;...yes
;
; check for stop key down
;
nnmi19
	jsr ud60        ;no .y
	jsr stop        ;no .y
	bne nnmi20      ;no stop key...test for rs232
;
; timb - where system goes on a brk instruction
;
timb	jsr restor      ;restore system indirects
	jsr ioinit      ;restore i/o for basic
	jsr cint        ;restore screen for basic
	jmp ($a002)     ;...no, so basic warm start

; disable nmi's untill ready
;  save on stack
;
nnmi20	tya             ;.y saved through restore
	and enabl       ;show only enables
	tax             ;save in .x for latter
;
; t1 nmi check - transmitt a bit
;
	and #$01        ;check for t1
	beq nnmi30      ;no...
;
	lda d2pra
	and #$ff-$04    ;fix for current i/o
	ora nxtbit      ;load data and...
	sta d2pra       ;...send it
;
	lda enabl       ;restore nmi's
	sta d2icr       ;ready for next...
;
; because of 6526 icr structure...
;  handle another nmi as a subroutine
;
	txa             ;test for another nmi
	and #$12        ;test for t2 or flag
	beq nnmi25
	and #$02        ;check for t2
	beq nnmi22      ;must be a flag
;
	jsr t2nmi       ;handle a normal bit in...
	jmp nnmi25      ;...then continue output
;
nnmi22	jsr flnmi       ;handle a start bit...
;
nnmi25	jsr rstrab      ;go calc info (code could be in line)
	jmp nmirti
;

; t2 nmi check - recieve a bit
;
nnmi30	txa
	and #$02        ;mask to t2
	beq nnmi40      ;no...
;
	jsr t2nmi       ;handle interrupt
	jmp nmirti

; flag nmi handler - recieve a start bit
;
nnmi40	txa             ;check for edge
	and #$10        ;on flag...
	beq nmirti      ;no...
;
	jsr flnmi       ;start bit routine

nmirti	lda enabl       ;restore nmi's
	sta d2icr
prend	pla             ;because of missing screen editor
	tay
	pla
	tax
	pla
	rti

; baudo table contains values
;  for 14.31818e6/14/baud rate/2 (ntsc)
;
baudo	.word 10277-cbit ; 50 baud
	.word 6818-cbit  ;   75   baud
	.word 4649-cbit  ;  110   baud
	.word 3800-cbit  ;  134.6 baud
	.word 3409-cbit  ;  150   baud
	.word 1705-cbit  ;  300   baud
	.word 852-cbit   ;  600   baud
	.word 426-cbit   ; 1200   baud
	.word 284-cbit   ; 1800   baud
	.word 213-cbit   ; 2400   baud
;
; cbit - an adjustment to make next t2 hit near center
;   of the next bit.
;   aprox the time to service a cb1 nmi
cbit	=100            ;cycles

; t2nmi - subroutine to handle an rs232
;  bit input.
;
t2nmi	lda d2prb       ;get data in
	and #01         ;mask off...
	sta inbit       ;...save for latter
;
; update t2 for mid bit check
;   (worst case <213 cycles to here)
;   (calc 125 cycles+43-66 dead)
;
	lda d2t2l       ;calc new time & clr nmi
	sbc #22+6
	adc baudof
	sta d2t2l
	lda d2t2h
	adc baudof+1
	sta d2t2h
;
	lda #$11        ;enable timer
	sta d2crb
;
	lda enabl       ;restore nmi's early...
	sta d2icr
;
	lda #$ff        ;enable count from $ffff
	sta d2t2l
	sta d2t2h
;
	jmp rsrcvr      ;go shift in...

; flnmi - subroutine to handle the
;  start bit timing..
;
; check for noise ?
;
flnmi
;
; get half bit rate value
;
	lda m51ajb
	sta d2t2l
	lda m51ajb+1
	sta d2t2h
;
	lda #$11        ;enable timer
	sta d2crb
;
	lda #$12        ;disable flag, enable t2
	eor enabl
	sta enabl
;ora #$82
;sta d2icr
;
	lda #$ff        ;preset for count down
	sta d2t2l
	sta d2t2h
;
	ldx bitnum      ;get #of bits in
	stx bitci       ;put in rcvrcnt
	rts
;
; popen - patches open rs232 for universal kernal
;
popen	tax             ;we're calculating baud rate
	lda m51ajb+1    ; m51ajb=freq/baud/2-100
	rol a
	tay
	txa
	adc #cbit+cbit
	sta baudof
	tya
	adc #0
	sta baudof+1
	rts
	nop
	nop

; rsr  8/02/80 - routine for panic
; rsr  8/08/80 - panic & stop key
; rsr  8/12/80 - change for a0int a subroutine
; rsr  8/19/80 - add rs-232 checks
; rsr  8/21/80 - modify rs-232
; rsr  8/29/80 - change panic order for jack
; rsr  8/30/80 - add t2
; rsr  9/22/80 - add 1800 baud opps!
; rsr 12/08/81 - modify for vic-40 system
; rsr 12/11/81 - continue modifications (vic-40)
; rsr 12/14/81 - modify for 6526 timer adjust
; rsr  2/09/82 - fix enable for flag nmi
; rsr  2/16/82 - rewrite for 6526 problems
; rsr  3/11/82 - change nmi renable, fix restore
; rsr  3/29/82 - enables are always or'ed with $80
