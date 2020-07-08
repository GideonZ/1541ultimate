jtp20	jsr zzz
	inc bufpt
	ldy bufpt
	cpy #bufsz
	rts

;stays in routine d2t1ll play switch
;
cste1	jsr cs10
	beq cs25
	ldy #ms7-ms1    ;"press play..."
cs30	jsr msg
cs40	jsr tstop       ;watch for stop key
	jsr cs10        ;watch cassette switches
	bne cs40
	ldy #ms18-ms1   ;"ok"
	jmp msg

;subr returns <> for cassette switch
;
cs10	lda #$10        ;check port
	bit r6510       ;closed?...
	bne cs25        ;no. . .
	bit r6510       ;check again to debounce
cs25	clc             ;good return
	rts

;checks for play & record
;
cste2	jsr cs10
	beq cs25
	ldy #ms8-ms1    ;"record"
	bne cs30

;read header block entry
;
rblk	lda #0
	sta status
	sta verck
	jsr ldad1

;read load block entry
;
trd	jsr cste1       ;say 'press play'
	bcs twrt3       ;stop key pressed
	sei
	lda #0          ;clear flags...
	sta rdflg
	sta snsw1
	sta cmp0
	sta ptr1
	sta ptr2
	sta dpsw
	lda #$90        ;enable for ca1 irq...read line
	ldx #14         ;point irq vector to read
	bne tape        ;jmp

;write header block entry
;
wblk	jsr ldad1
;
;write load block entry
;
twrt	lda #20         ;between block shorts
	sta shcnh
twrt2	jsr cste2       ;say 'press play & record'
twrt3	bcs stop3       ;stop key pressed
	sei
	lda #$82        ;enable t2 irqs...write time
	ldx #8          ;vector irq to wrtz

;start tape operation entry point
;
tape	ldy #$7f        ;kill unwanted irq's
	sty d1icr
	sta d1icr       ;turn on wanted
	lda d1cra       ;calc timer enables
	ora #$19
	sta d1crb       ;turn on t2 irq's for cass write(one shot)
	and #$91        ;save tod 50/60 indication
	sta caston      ;place in auto mode for t1
; wait for rs-232 to finish
	jsr rsp232
; disable screen display
	lda vicreg+17
	and #$ff-$10    ;disable screen
	sta vicreg+17
; move irq to irqtemp for cass ops
	lda cinv
	sta irqtmp
	lda cinv+1
	sta irqtmp+1
	jsr bsiv        ;go change irq vector
	lda #2          ;fsblk starts at 2
	sta fsblk
	jsr newch       ;prep local counters and flags
	lda r6510       ;turn motor on
	and #%011111    ;low turns on
	sta r6510
	sta cas1        ;flag internal control of cass motor
	ldx #$ff        ;delay between blocks
tp32	ldy #$ff
tp35	dey
	bne tp35
	dex
	bne tp32
	cli
tp40	lda irqtmp+1    ;check for interrupt vector...
	cmp cinv+1      ;...pointing at key routine
	clc
	beq stop3       ;...yes return
	jsr tstop       ;...no check for stop key
;
; 60 hz keyscan ignored
;
	jsr ud60        ; stop key check
	jmp tp40        ;stay in loop untill tapes are done

tstop	jsr stop        ;stop key down?
	clc             ;assume no stop
	bne stop4       ;we were right
;
;stop key down...
;
	jsr tnif        ;turn off cassettes
	sec             ;failure flag
	pla             ;back one square...
	pla
;
; lda #0 ;stop key flag
;
stop3	lda #0          ;deallocate irqtmp
	sta irqtmp+1    ;if c-set then stop key
stop4	rts

;
; stt1 - set up timeout watch for next dipole
;
stt1	stx temp        ;.x has constant for timeout
	lda cmp0        ;cmp0*5
	asl a
	asl a
	clc
	adc cmp0
	clc 
	adc temp        ;adjust long byte count
	sta temp
	lda #0
	bit cmp0        ;check cmp0 ...
	bmi stt2        ;...minus, no adjust
	rol a           ;...plus so adjust pos
stt2	asl temp        ;multiply corrected value by 4
	rol a
	asl temp
	rol a
	tax
stt3	lda d1t2l       ;watch out for d1t2h rollover...
	cmp #22         ;...time for routine...!!!...
	bcc stt3        ;...too close so wait untill past
	adc temp        ;calculate and...
	sta d1t1l       ;...store adusted time count
	txa
	adc d1t2h       ;adjust for high time count
	sta d1t1h
	lda caston      ;enable timers
	sta d1cra
	sta stupid      ;non-zero means an t1 irq has not occured yet
	lda d1icr       ;clear old t1 interrupt
	and #$10        ;check for old-flag irq
	beq stt4        ;no...normal exit
	lda #>stt4      ;push simulated return address on stack
	pha
	lda #<stt4
	pha
	jmp simirq
stt4	cli             ;allow for re-entry code
	rts

; rsr 8/25/80 modify i/o for mod2 hardware
; rsr 12/11/81 modify i/o for vic-40
; rsr 2/9/82 add screen disable for tape
; rsr 3/28/82 add t2irq to start cassette write
; rsr 3/28/82 add cassette read timer1 flag
; rsr 5/11/82 change so we don't miss any irq's
; rsr 5/14/82 simulate an irq
